/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RepresentationChooserManualOSD.h"

#include "../utils/SettingsUtils.h"
#include "../utils/log.h"
#include "RepresentationSelector.h"

using namespace adaptive;
using namespace CHOOSER;
using namespace UTILS;

CRepresentationChooserManualOSD::CRepresentationChooserManualOSD()
{
  LOG::Log(LOGDEBUG, "[Repr. chooser] Type: Manual OSD");
}

void CRepresentationChooserManualOSD::Initialize(const UTILS::PROPERTIES::ChooserProps& props)
{
  std::string manualSelMode{kodi::addon::GetSettingString("adaptivestream.streamselection.mode")};

  if (manualSelMode == "manual-v")
    m_streamSelectionMode = SETTINGS::StreamSelection::MANUAL_VIDEO_ONLY;
  else
    m_streamSelectionMode = SETTINGS::StreamSelection::MANUAL;

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

  LOG::Log(LOGDEBUG,
           "[Repr. chooser] Configuration\n"
           "Stream manual selection mode: %s\n"
           "Resolution max: %ix%i\n"
           "Resolution max for secure decoder: %ix%i",
           manualSelMode.c_str(), m_screenResMax.first, m_screenResMax.second,
           m_screenResSecureMax.first, m_screenResSecureMax.second);
}

void CRepresentationChooserManualOSD::RefreshResolution()
{
  m_screenWidth = m_screenCurrentWidth;
  m_screenHeight = m_screenCurrentHeight;

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

void CRepresentationChooserManualOSD::PostInit()
{
  RefreshResolution();

  LOG::Log(LOGDEBUG,
           "[Repr. chooser] Stream selection conditions\n"
           "Resolution: %ix%i",
           m_screenWidth, m_screenHeight);
}

AdaptiveTree::Representation* CRepresentationChooserManualOSD::ChooseRepresentation(
    AdaptiveTree::AdaptationSet* adp)
{
  CRepresentationSelector selector(m_screenWidth, m_screenHeight);

  return selector.Highest(adp);
}

AdaptiveTree::Representation* CRepresentationChooserManualOSD::ChooseNextRepresentation(
    AdaptiveTree::AdaptationSet* adp, AdaptiveTree::Representation* currentRep)
{
  return currentRep;
}
