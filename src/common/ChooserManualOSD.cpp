/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ChooserManualOSD.h"

#include "AdaptationSet.h"
#include "CompKodiProps.h"
#include "CompSettings.h"
#include "ReprSelector.h"
#include "Representation.h"
#include "SrvBroker.h"
#include "utils/log.h"

using namespace ADP;
using namespace CHOOSER;
using namespace PLAYLIST;

CRepresentationChooserManualOSD::CRepresentationChooserManualOSD()
{
  LOG::Log(LOGDEBUG, "[Repr. chooser] Type: Manual OSD");
}

void CRepresentationChooserManualOSD::Initialize(const ADP::KODI_PROPS::ChooserProps& props)
{
  auto& settings = CSrvBroker::GetSettings();

  SETTINGS::StreamSelMode manualSelMode = settings.GetStreamSelMode();

  if (manualSelMode == SETTINGS::StreamSelMode::MANUAL_VIDEO)
    m_streamSelectionMode = StreamSelection::MANUAL_VIDEO_ONLY;
  else
    m_streamSelectionMode = StreamSelection::MANUAL;

  m_screenResMax = settings.GetResMax();
  m_screenResSecureMax = settings.GetResSecureMax();

  LOG::Log(LOGDEBUG,
           "[Repr. chooser] Configuration\n"
           "Stream manual selection mode: %i\n"
           "Resolution max: %ix%i\n"
           "Resolution max for secure decoder: %ix%i",
           manualSelMode, m_screenResMax.first, m_screenResMax.second,
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

void CRepresentationChooserManualOSD::SetSecureSession(const bool isSecureSession)
{
  m_isSecureSession = isSecureSession;
  RefreshResolution();
}

void CRepresentationChooserManualOSD::PostInit()
{
  RefreshResolution();

  LOG::Log(LOGDEBUG,
           "[Repr. chooser] Stream selection conditions\n"
           "Resolution: %ix%i",
           m_screenWidth, m_screenHeight);
}

PLAYLIST::CRepresentation* CRepresentationChooserManualOSD::GetNextRepresentation(
    PLAYLIST::CAdaptationSet* adp, PLAYLIST::CRepresentation* currentRep)
{
  if (currentRep)
    return currentRep;

  CRepresentationSelector selector(m_screenWidth, m_screenHeight);
  return selector.Highest(adp);
}
