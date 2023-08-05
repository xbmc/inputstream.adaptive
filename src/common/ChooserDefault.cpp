/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ChooserDefault.h"

#include "../utils/SettingsUtils.h"
#include "../utils/log.h"
#include "ReprSelector.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string_view>

using namespace CHOOSER;
using namespace PLAYLIST;
using namespace UTILS;

namespace
{

constexpr const long long SCREEN_RES_REFRESH_SECS = 10;

} // unnamed namespace

CRepresentationChooserDefault::CRepresentationChooserDefault()
{
  LOG::Log(LOGDEBUG, "[Repr. chooser] Type: Default");
}

void CRepresentationChooserDefault::Initialize(const UTILS::PROPERTIES::ChooserProps& props)
{
  std::pair<int, int> res;
  if (SETTINGS::ParseResolutionLimit(kodi::addon::GetSettingString("adaptivestream.res.max"), res))
  {
    m_screenResMax = res;
  }
  if (SETTINGS::ParseResolutionLimit(kodi::addon::GetSettingString("adaptivestream.res.secure.max"),
                                     res))
  {
    m_screenResSecureMax = res;
  }

  m_bandwidthInitAuto = kodi::addon::GetSettingBoolean("adaptivestream.bandwidth.init.auto");
  m_bandwidthInit =
      static_cast<uint32_t>(kodi::addon::GetSettingInt("adaptivestream.bandwidth.init") * 1000);

  m_bandwidthMin =
      static_cast<uint32_t>(kodi::addon::GetSettingInt("adaptivestream.bandwidth.min") * 1000);
  m_bandwidthMax =
      static_cast<uint32_t>(kodi::addon::GetSettingInt("adaptivestream.bandwidth.max") * 1000);

  m_ignoreScreenRes = kodi::addon::GetSettingBoolean("overrides.ignore.screen.res");
  m_ignoreScreenResChange = kodi::addon::GetSettingBoolean("overrides.ignore.screen.res.change");

  // Override settings with Kodi/video add-on properties

  if (m_bandwidthMax == 0 || (props.m_bandwidthMax > 0 && m_bandwidthMax > props.m_bandwidthMax))
  {
    m_bandwidthMax = props.m_bandwidthMax;
  }

  if (m_screenResMax.first == 0 ||
      (props.m_resolutionMax.first > 0 && m_screenResMax > props.m_resolutionMax))
  {
    m_screenResMax = props.m_resolutionMax;
  }

  if (m_screenResSecureMax.first == 0 ||
      (props.m_resolutionSecureMax.first > 0 && m_screenResSecureMax > props.m_resolutionSecureMax))
  {
    m_screenResSecureMax = props.m_resolutionSecureMax;
  }

  LOG::Log(LOGDEBUG,
           "[Repr. chooser] Configuration\n"
           "Resolution max: %ix%i\n"
           "Resolution max for secure decoder: %ix%i\n"
           "Bandwidth limits (bit/s): min %u, max %u\n"
           "Ignore screen resolution: %i\n"
           "Ignore screen resolution change: %i",
           m_screenResMax.first, m_screenResMax.second, m_screenResSecureMax.first,
           m_screenResSecureMax.second, m_bandwidthMin, m_bandwidthMax, m_ignoreScreenRes,
           m_ignoreScreenResChange);
}

void CRepresentationChooserDefault::SetSecureSession(const bool isSecureSession)
{
  m_isSecureSession = isSecureSession;
  RefreshResolution();
}

void CRepresentationChooserDefault::PostInit()
{
  RefreshResolution();

  if (!m_bandwidthInitAuto)
  {
    m_bandwidthCurrent = std::max(m_bandwidthInit, m_bandwidthMin);
    m_bandwidthCurrentLimited = m_bandwidthCurrent;
  }
  else if (m_bandwidthCurrent == 0)
  {
    LOG::Log(
        LOGDEBUG,
        "[Repr. chooser] The initial bandwidth cannot be determined due to download speed at 0. "
        "Fallback to default user setting.");
    m_bandwidthCurrent = std::max(m_bandwidthInit, m_bandwidthMin);
  }

  LOG::Log(LOGDEBUG,
           "[Repr. chooser] Stream selection conditions\n"
           "Screen resolution: %ix%i (may be limited by settings)\n"
           "Initial bandwidth: %u bit/s",
           m_screenWidth, m_screenHeight, m_bandwidthCurrent);
}

void CRepresentationChooserDefault::CheckResolution()
{
  if (m_screenWidth != m_screenCurrentWidth || m_screenHeight != m_screenCurrentHeight)
  {
    // Update the screen resolution values only after n seconds
    // to prevent too fast update when Kodi window will be resized
    if (m_screenResLastUpdate.has_value() &&
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                         *m_screenResLastUpdate)
                .count() < SCREEN_RES_REFRESH_SECS)
    {
      return;
    }
    RefreshResolution();
    m_screenResLastUpdate = std::chrono::steady_clock::now();
    LOG::Log(LOGDEBUG, "[Repr. chooser] Screen resolution has changed: %ix%i", m_screenCurrentWidth,
             m_screenCurrentHeight);
  }
}

void CRepresentationChooserDefault::RefreshResolution()
{
  m_screenWidth = m_ignoreScreenRes ? 16384 : m_screenCurrentWidth;
  m_screenHeight = m_ignoreScreenRes ? 16384 : m_screenCurrentHeight;

  // If set, limit resolution to user choice
  const auto& userResLimit{m_isSecureSession ? m_screenResSecureMax : m_screenResMax};

  if (userResLimit.first > 0 && userResLimit.second > 0)
  {
    if (m_screenWidth > userResLimit.first)
      m_screenWidth = userResLimit.first;

    if (m_screenHeight > userResLimit.second)
      m_screenHeight = userResLimit.second;
  }
}

void CRepresentationChooserDefault::SetDownloadSpeed(const double speed)
{
  m_downloadSpeedChron.push_back(speed);

  // Calculate the average speed of last 10 download speeds
  if (m_downloadSpeedChron.size() > 10)
    m_downloadSpeedChron.pop_front();

  // Calculate the current bandwidth from the average download speed
  if (m_bandwidthCurrent == 0)
    m_bandwidthCurrent = static_cast<uint32_t>(speed * 8);
  else
  {
    double avgSpeedBytes{
        std::accumulate(m_downloadSpeedChron.begin(), m_downloadSpeedChron.end(), 0.0) /
        m_downloadSpeedChron.size()};
    m_bandwidthCurrent = static_cast<uint32_t>(avgSpeedBytes * 8);
  }

  // Force the bandwidth to the limits set by the user or add-on
  m_bandwidthCurrentLimited = m_bandwidthCurrent;
  if (m_bandwidthMin > 0 && m_bandwidthCurrent < m_bandwidthMin)
    m_bandwidthCurrentLimited = m_bandwidthMin;
  if (m_bandwidthMax > 0 && m_bandwidthCurrent > m_bandwidthMax)
    m_bandwidthCurrentLimited = m_bandwidthMax;
}

PLAYLIST::CRepresentation* CRepresentationChooserDefault::GetNextRepresentation(
    PLAYLIST::CAdaptationSet* adp, PLAYLIST::CRepresentation* currentRep)
{
  bool isVideoStreamType = adp->GetStreamType() == StreamType::VIDEO;

  if (isVideoStreamType && !m_ignoreScreenRes && !m_ignoreScreenResChange)
    CheckResolution();

  CRepresentationSelector selector(m_screenWidth, m_screenHeight);
  uint32_t bandwidth;

  // From bandwidth take in consideration:
  // 90% of bandwidth for video - 10 % for other
  if (isVideoStreamType)
    bandwidth = static_cast<uint32_t>(m_bandwidthCurrentLimited * 0.9);
  else
    bandwidth = static_cast<uint32_t>(m_bandwidthCurrentLimited * 0.1);

  CRepresentation* nextRep{nullptr};
  int bestScore{-1};

  for (auto& rep : adp->GetRepresentations())
  {
    int score{std::abs(rep->GetWidth() * rep->GetHeight() - m_screenWidth * m_screenHeight)};

    if (!m_isForceStartsMaxRes)
    {
      if (rep->GetBandwidth() > bandwidth)
        continue;

      score += static_cast<int>(std::sqrt(bandwidth - rep->GetBandwidth()));
    }

    if (bestScore == -1 || score < bestScore)
    {
      bestScore = score;
      nextRep = rep.get();
    }
  }

  if (!nextRep)
    nextRep = selector.Lowest(adp);

  if (isVideoStreamType) // Only video, to avoid fill too much the log
  {
    LOG::Log(LOGDEBUG, "[Repr. chooser] Current average bandwidth: %u bit/s (filtered to %u bit/s)",
             m_bandwidthCurrent, bandwidth);
    LogDetails(currentRep, nextRep);
  }

  if (m_isForceStartsMaxRes)
    m_isForceStartsMaxRes = false;

  return nextRep;
}
