/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RepresentationChooserDefault.h"

#include "../utils/log.h"

#include <cmath>

using namespace adaptive;

namespace
{

constexpr const long long SCREEN_RES_REFRESH_SECS = 15;
// Bandwidth with which to select the initial representation (4 Mbps)
constexpr const uint32_t INITIAL_BANDWIDTH = 4000000;

} // unnamed namespace

CRepresentationChooserDefault::CRepresentationChooserDefault(std::string kodiProfilePath)
{
  LOG::Log(LOGDEBUG, "[Repr. chooser] Type: Default");

  //! @todo Remove retrieving/saving average bandwidth to disk
  //! this value not reflect actual network bandwidth
  if (!kodiProfilePath.empty())
  {
    m_bandwidthFilePath = kodiProfilePath + "bandwidth.bin";

    FILE* f = fopen(m_bandwidthFilePath.c_str(), "rb");
    if (f)
    {
      double val;
      size_t sz(fread(&val, sizeof(double), 1, f));
      if (sz)
      {
        m_bandwidthStored = static_cast<uint32_t>(val * 8);
        SetDownloadSpeed(val);
      }
      fclose(f);
    }
  }
}

CRepresentationChooserDefault::~CRepresentationChooserDefault()
{
  //! @todo Remove retrieving/saving average bandwidth to disk
  //! this value not reflect actual network bandwidth
  if (!m_bandwidthFilePath.empty())
  {
    std::string fn(m_bandwidthFilePath + "bandwidth.bin");
    FILE* f = fopen(fn.c_str(), "wb");
    if (f)
    {
      double val(m_downloadAverageSpeed);
      fwrite((const char*)&val, sizeof(double), 1, f);
      fclose(f);
    }
  }
}

void CRepresentationChooserDefault::Initialize(const UTILS::PROPERTIES::KodiProperties& kodiProps)
{
  if (m_bandwidthStored == 0)
    m_bandwidthStored = INITIAL_BANDWIDTH;

#ifndef INPUTSTREAM_TEST_BUILD

  m_bufferDurationAssured = kodi::addon::GetSettingInt("ASSUREDBUFFERDURATION");
  m_bufferDurationMax = kodi::addon::GetSettingInt("MAXBUFFERDURATION");

  m_screenWidthMax = kodi::addon::GetSettingInt("MAXRESOLUTION");
  m_screenWidthMaxSecure = kodi::addon::GetSettingInt("MAXRESOLUTIONSECURE");

  m_bandwidthMin = kodi::addon::GetSettingInt("MINBANDWIDTH");
  m_bandwidthMax = kodi::addon::GetSettingInt("MAXBANDWIDTH");

  m_ignoreScreenRes = kodi::addon::GetSettingBoolean("IGNOREDISPLAY");
  m_ignoreScreenResChange = kodi::addon::GetSettingBoolean("IGNOREWINDOWCHANGE");

  m_isHdcpOverride = kodi::addon::GetSettingBoolean("HDCPOVERRIDE");

#endif

  if (m_bandwidthMax == 0 ||
      (kodiProps.m_bandwidthMax && m_bandwidthMax > kodiProps.m_bandwidthMax))
  {
    m_bandwidthMax = kodiProps.m_bandwidthMax;
  }

  LOG::Log(LOGDEBUG,
           "[Repr. chooser] Configuration\n"
           "Resolution max: %i\n"
           "Resolution max for secure decoder: %i\n"
           "Bandwidth limits: min %u, max %u\n"
           "Ignore display res.: %i\n"
           "Ignore resolution change: %i\n"
           "HDCP override: %i",
           m_screenWidthMax, m_screenWidthMaxSecure, m_bandwidthMin, m_bandwidthMax,
           m_ignoreScreenRes, m_ignoreScreenResChange, m_isHdcpOverride);
}

void CRepresentationChooserDefault::PostInit()
{
  LOG::Log(LOGDEBUG,
           "[Repr. chooser] Stream selection conditions\n"
           "Resolution: %ix%i\n"
           "Initial bandwidth: %u\n"
           "Buffer duration: assured %u secs, max %u secs",
           m_screenSelWidth, m_screenSelHeight, m_bandwidthStored, m_bufferDurationAssured,
           m_bufferDurationMax);
}

//SetScreenResolution will be called upon changed video resolution only (will be filtered beforehand by xbmc api calls to SetVideoResolution)
void CRepresentationChooserDefault::SetScreenResolution(int width, int height)
{
  if (m_isScreenResNeedUpdate)
  {
    m_screenCurrentWidth = width;
    m_screenCurrentHeight = height;
    //LOG::Log(LOGDEBUG, "[Repr. chooser] Screen resolution set to: %ux%u", width, height);

    m_screenSelWidth = m_ignoreScreenRes ? 8192 : m_screenCurrentWidth;
    switch (m_isSecureSession ? m_screenWidthMaxSecure : m_screenWidthMax)
    {
      case 1:
        if (m_screenSelWidth > 640)
          m_screenSelWidth = 640;
        break;
      case 2:
        if (m_screenSelWidth > 960)
          m_screenSelWidth = 960;
        break;
      case 3:
        if (m_screenSelWidth > 1280)
          m_screenSelWidth = 1280;
        break;
      case 4:
        if (m_screenSelWidth > 1920)
          m_screenSelWidth = 1920;
        break;
      default:
        break;
    }

    m_screenSelHeight = m_ignoreScreenRes ? 8192 : m_screenCurrentHeight;
    switch (m_isSecureSession ? m_screenWidthMaxSecure : m_screenWidthMax)
    {
      case 1:
        if (m_screenSelHeight > 480)
          m_screenSelHeight = 480;
        break;
      case 2:
        if (m_screenSelHeight > 640)
          m_screenSelHeight = 640;
        break;
      case 3:
        if (m_screenSelHeight > 720)
          m_screenSelHeight = 720;
        break;
      case 4:
        if (m_screenSelHeight > 1080)
          m_screenSelHeight = 1080;
        break;
      default:
        break;
    }

    m_screenNextWidth = m_screenCurrentWidth;
    m_screenNextHeight = m_screenCurrentHeight;
    m_isScreenResNeedUpdate = false;
  }
  else
  {
    m_screenNextWidth = width;
    m_screenNextHeight = height;
  }
  m_screenResLastUpdate = std::chrono::steady_clock::now();
}

void CRepresentationChooserDefault::SetDownloadSpeed(double speed)
{
  m_downloadCurrentSpeed = speed;
  if (!m_downloadAverageSpeed)
    m_downloadAverageSpeed = m_downloadCurrentSpeed;
  else
    m_downloadAverageSpeed = m_downloadAverageSpeed * 0.8 + m_downloadCurrentSpeed * 0.2;
}

void CRepresentationChooserDefault::AddDecrypterCaps(const SSD::SSD_DECRYPTER::SSD_CAPS& ssdCaps)
{
  m_decrypterCaps.emplace_back(ssdCaps);
}

AdaptiveTree::Representation* CRepresentationChooserDefault::ChooseRepresentation(
    AdaptiveTree::AdaptationSet* adp)
{
  AdaptiveTree::Representation* newRep{nullptr};

  uint32_t bandwidth = m_bandwidthMin;
  if (m_bandwidthCurrent > m_bandwidthStored)
    bandwidth = m_bandwidthCurrent;
  if (m_bandwidthMax && m_bandwidthStored > m_bandwidthMax)
    bandwidth = m_bandwidthMax;

  bandwidth =
      static_cast<uint32_t>(m_bandwidthStored * (adp->type_ == AdaptiveTree::VIDEO ? 0.9 : 0.1));

  int valScore{-1};
  int bestScore{-1};
  uint16_t hdcpVersion{99};
  int hdcpLimit{0};

  for (auto rep : adp->representations_)
  {
    if (!rep)
    {
      LOG::LogF(LOGERROR, "The representation pointer is nullptr");
      continue;
    }

    // Set buffer durations to representation
    //! @todo: buffers currently unused
    rep->assured_buffer_duration_ = m_bufferDurationAssured;
    rep->max_buffer_duration_ = m_bufferDurationMax;

    if (!m_isHdcpOverride)
    {
      hdcpVersion = m_decrypterCaps[rep->pssh_set_].hdcpVersion;
      hdcpLimit = m_decrypterCaps[rep->pssh_set_].hdcpLimit;
    }

    int score = std::abs(rep->width_ * rep->height_ - m_screenSelWidth * m_screenSelHeight) +
                static_cast<int>(std::sqrt(bandwidth - rep->bandwidth_));

    if (rep->bandwidth_ <= bandwidth && rep->hdcpVersion_ <= hdcpVersion &&
        (hdcpLimit == 0 || rep->width_ * rep->height_ <= hdcpLimit) &&
        (bestScore == -1 || score < bestScore))
    {
      bestScore = score;
      newRep = rep;
    }
    else if (!adp->min_rep_ || rep->bandwidth_ < adp->min_rep_->bandwidth_)
    {
      adp->min_rep_ = rep;
    }

    // It is bandwidth independent (if multiple same resolution bandwidth, will select first rep)
    score = std::abs(rep->width_ * rep->height_ - m_screenSelWidth * m_screenSelHeight);

    if (rep->hdcpVersion_ <= hdcpVersion &&
        (hdcpLimit == 0 || rep->width_ * rep->height_ <= hdcpLimit) &&
        (valScore == -1 || score <= valScore))
    {
      valScore = score;
      if (!adp->best_rep_ || rep->bandwidth_ > adp->best_rep_->bandwidth_)
        adp->best_rep_ = rep;
    }
  }

  if (!newRep)
    newRep = adp->min_rep_;

  if (!adp->best_rep_)
    adp->best_rep_ = adp->min_rep_;

  return newRep;
}

//! @todo: to be called from ensuresegment method only, SEPERATED FOR FURTHER DEVELOPMENT, CAN BE MERGED AFTERWARDS
AdaptiveTree::Representation* CRepresentationChooserDefault::ChooseNextRepresentation(
    AdaptiveTree::AdaptationSet* adp,
    AdaptiveTree::Representation* rep,
    size_t& validSegmentBuffers,
    size_t& availableSegmentBuffers,
    uint32_t& bufferLengthAssured,
    uint32_t& bufferLengthMax,
    uint32_t repCounter)
{
  if (!m_ignoreScreenResChange && !m_ignoreScreenRes &&
      !(m_screenNextWidth == m_screenCurrentWidth && m_screenNextHeight == m_screenCurrentHeight))
  {
    // Update resolution values only after n seconds
    if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                         m_screenResLastUpdate)
            .count() > SCREEN_RES_REFRESH_SECS)
    {
      m_isScreenResNeedUpdate = true;
      LOG::Log(LOGDEBUG, "[Repr. chooser] Updating display resolution to: %ix%i", m_screenNextWidth,
               m_screenNextHeight);
      SetScreenResolution(m_screenNextWidth, m_screenNextHeight);
    }
  }

  AdaptiveTree::Representation* nextRep{nullptr}; //nextRep definition to be finalised

  m_bandwidthCurrent = m_downloadAverageSpeed;
  LOG::Log(LOGDEBUG, "[Repr. chooser] Current bandwidth: %u", m_bandwidthCurrent);

  float bufferHungryFactor = 1.0; // can be made as a sliding input
  bufferHungryFactor = static_cast<float>(validSegmentBuffers) / bufferLengthAssured;
  bufferHungryFactor = bufferHungryFactor > 0.5 ? bufferHungryFactor : 0.5;

  uint32_t bandwidth = static_cast<uint32_t>(bufferHungryFactor * 7.0 * m_bandwidthCurrent);
  LOG::Log(LOGDEBUG, "[Repr. chooser] Bandwidth set: %u", bandwidth);

  if (validSegmentBuffers >= bufferLengthAssured)
  {
    return adp->best_rep_;
  }
  if ((validSegmentBuffers > 6) && (bandwidth >= rep->bandwidth_ * 2) && (rep != adp->best_rep_) &&
      (adp->best_rep_->bandwidth_ <= bandwidth)) //overwrite case, more internet data
  {
    validSegmentBuffers = std::max(validSegmentBuffers / 2, validSegmentBuffers - repCounter);
    availableSegmentBuffers = validSegmentBuffers; //so that ensure writes again with new rep
  }

  int bestScore{-1};
  uint16_t hdcpVersion{99};
  int hdcpLimit{0};

  for (auto rep : adp->representations_)
  {
    if (!rep)
    {
      LOG::LogF(LOGERROR, "The representation pointer is nullptr");
      continue;
    }

    if (!m_isHdcpOverride)
    {
      hdcpVersion = m_decrypterCaps[rep->pssh_set_].hdcpVersion;
      hdcpLimit = m_decrypterCaps[rep->pssh_set_].hdcpLimit;
    }

    int score = std::abs(rep->width_ * rep->height_ - m_screenSelWidth * m_screenSelHeight) +
                static_cast<int>(std::sqrt(bandwidth - rep->bandwidth_));

    if (rep->bandwidth_ <= bandwidth && rep->hdcpVersion_ <= hdcpVersion &&
        (hdcpLimit == 0 || rep->width_ * rep->height_ <= hdcpLimit) &&
        (bestScore == -1 || score < bestScore))
    {
      bestScore = score;
      nextRep = rep;
    }
    else if (!adp->min_rep_ || rep->bandwidth_ < adp->min_rep_->bandwidth_)
    {
      adp->min_rep_ = rep;
    }
  }

  if (!nextRep)
    nextRep = adp->min_rep_;

  // LOG::Log(LOGDEBUG, "[Repr. chooser] Next rep. bandwidth: %u", nextRep->m_bandwidthStored);

  return nextRep;
}
