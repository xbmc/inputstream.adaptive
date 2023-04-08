/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ChooserTest.h"

#include "../utils/SettingsUtils.h"
#include "../utils/log.h"
#include "ReprSelector.h"

using namespace CHOOSER;
using namespace PLAYLIST;
using namespace UTILS;

CRepresentationChooserTest::CRepresentationChooserTest()
{
  LOG::Log(LOGDEBUG, "[Repr. chooser] Type: Test");
}

void CRepresentationChooserTest::Initialize(const UTILS::PROPERTIES::ChooserProps& props)
{
  std::string manualSelMode{kodi::addon::GetSettingString("adaptivestream.streamselection.mode")};

  if (manualSelMode == "manual-v")
    m_streamSelectionMode = SETTINGS::StreamSelection::MANUAL_VIDEO_ONLY;
  else
    m_streamSelectionMode = SETTINGS::StreamSelection::MANUAL;

  std::string testMode{kodi::addon::GetSettingString("adaptivestream.test.mode")};

  if (testMode == "switch-segments")
    m_testMode = TestMode::SWITCH_SEGMENTS;
  else // Fallback
    m_testMode = TestMode::SWITCH_SEGMENTS;

  std::string logDetails;

  if (m_testMode == TestMode::SWITCH_SEGMENTS)
  {
    m_segmentsLimit = kodi::addon::GetSettingInt("adaptivestream.test.segments");
    logDetails = kodi::tools::StringUtils::Format("Segments: %i", m_segmentsLimit);
  }

  LOG::Log(LOGDEBUG,
           "[Repr. chooser] Configuration\n"
           "Test mode: %s\n%s",
           testMode.c_str(), logDetails.c_str());
}

void CRepresentationChooserTest::PostInit()
{
}

PLAYLIST::CRepresentation* CRepresentationChooserTest::GetNextRepresentation(
    PLAYLIST::CAdaptationSet* adp, PLAYLIST::CRepresentation* currentRep)
{
  CRepresentationSelector selector(m_screenCurrentWidth, m_screenCurrentHeight);
  CRepresentation* nextRep{currentRep};

  if (!currentRep) // Startup or new period
  {
    m_segmentsElapsed = 1;

    if (m_testMode == TestMode::SWITCH_SEGMENTS)
    {
      nextRep = selector.Lowest(adp);
    }
    else
    {
      LOG::LogF(LOGERROR, "Unhandled test mode");
    }
  }
  else
  {
    if (m_testMode == TestMode::SWITCH_SEGMENTS)
    {
      if (adp->GetStreamType() != StreamType::VIDEO)
        return currentRep;

      m_segmentsElapsed += 1;
      if (m_segmentsElapsed > m_segmentsLimit)
      {
        m_segmentsElapsed = 1;
        nextRep = selector.Higher(adp, currentRep);
        // If there are no next representations, start again from the lowest
        if (nextRep == currentRep)
          nextRep = selector.Lowest(adp);
      }
    }
  }

  if (adp->GetStreamType() == StreamType::VIDEO)
    LogDetails(currentRep, nextRep);

  return nextRep;
}
