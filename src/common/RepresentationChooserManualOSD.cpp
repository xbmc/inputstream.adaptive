/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RepresentationChooserManualOSD.h"

#include "../utils/log.h"
#include "RepresentationSelector.h"

using namespace CHOOSER;
using namespace adaptive;
using namespace UTILS;

CRepresentationChooserManualOSD::CRepresentationChooserManualOSD()
{
  LOG::Log(LOGDEBUG, "[Repr. chooser] Type: Manual OSD");
}

void CRepresentationChooserManualOSD::Initialize(const UTILS::PROPERTIES::KodiProperties& kodiProps)
{
  std::string manualSelMode{kodi::addon::GetSettingString("adaptivestream.streamselection.mode")};

  if (manualSelMode == "manual-v")
    m_streamSelectionMode = SETTINGS::StreamSelection::MANUAL_VIDEO_ONLY;
  else
    m_streamSelectionMode = SETTINGS::StreamSelection::MANUAL;

  m_screenWidthMax = kodi::addon::GetSettingString("adaptivestream.res.max");
  m_screenWidthMaxSecure = kodi::addon::GetSettingString("adaptivestream.res.max.secure");

  LOG::Log(LOGDEBUG,
           "[Repr. chooser] Configuration\n"
           "Stream manual selection mode: %s\n"
           "Resolution max: %s\n"
           "Resolution max for secure decoder: %s",
           manualSelMode.c_str(), m_screenWidthMax.c_str(), m_screenWidthMaxSecure.c_str());
}

void CRepresentationChooserManualOSD::RefreshResolution()
{
  m_screenWidth = m_screenCurrentWidth;
  m_screenHeight = m_screenCurrentHeight;

  // If set, limit resolution to user choice
  std::string_view userMaxRes{m_isSecureSession ? m_screenWidthMaxSecure : m_screenWidthMax};

  auto mapIt{RESOLUTION_LIMITS.find(userMaxRes)};

  if (mapIt != RESOLUTION_LIMITS.end())
  {
    const std::pair<int, int>& resLimit{mapIt->second};

    if (m_screenWidth > resLimit.first)
      m_screenWidth = resLimit.first;

    if (m_screenHeight > resLimit.second)
      m_screenHeight = resLimit.second;
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
