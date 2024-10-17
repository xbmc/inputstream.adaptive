/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Chooser.h"

#include "CompResources.h"
#include "ChooserAskQuality.h"
#include "ChooserDefault.h"
#include "ChooserFixedRes.h"
#include "ChooserManualOSD.h"
#include "ChooserTest.h"
#include "CompKodiProps.h"
#include "CompSettings.h"
#include "Representation.h"
#include "SrvBroker.h"
#include "utils/log.h"

#ifndef INPUTSTREAM_TEST_BUILD
#include <kodi/gui/General.h>
#endif

#include <vector>

using namespace ADP;
using namespace CHOOSER;
using namespace PLAYLIST;

namespace
{
IRepresentationChooser* GetReprChooser(std::string_view type)
{
  // Chooser's names are used for add-on settings and Kodi properties
  if (type == "default" || type == "adaptive")
    return new CRepresentationChooserDefault();
  else if (type == "fixed-res")
    return new CRepresentationChooserFixedRes();
  else if (type == "ask-quality")
    return new CRepresentationChooserAskQuality();
  else if (type == "manual-osd")
    return new CRepresentationChooserManualOSD();
  else if (type == "test")
    return new CRepresentationChooserTest();
  else
    return nullptr;
}
} // unnamed namespace

IRepresentationChooser* CHOOSER::CreateRepresentationChooser()
{
  IRepresentationChooser* reprChooser{nullptr};

  const KODI_PROPS::ChooserProps& props = CSrvBroker::GetKodiProps().GetChooserProps();

  // An add-on can override XML settings by using Kodi properties
  if (!props.m_chooserType.empty())
  {
    reprChooser = GetReprChooser(props.m_chooserType);
    if (!reprChooser)
      LOG::Log(LOGERROR, "Stream selection type \"%s\" not exist. Fallback to XML settings");
  }

  if (!reprChooser)
    reprChooser = GetReprChooser(CSrvBroker::GetSettings().GetChooserType());

  // Safe check for wrong settings, fallback to default
  if (!reprChooser)
    reprChooser = new CRepresentationChooserDefault();

  reprChooser->OnUpdateScreenRes();
  reprChooser->Initialize(props);

  return reprChooser;
}

CHOOSER::IRepresentationChooser::IRepresentationChooser()
{
  AdjustRefreshRateStatus adjRefreshRate{kodi::gui::GetAdjustRefreshRateStatus()};

  if (adjRefreshRate == AdjustRefreshRateStatus::ADJUST_REFRESHRATE_STATUS_ON_START ||
      adjRefreshRate == AdjustRefreshRateStatus::ADJUST_REFRESHRATE_STATUS_ON_STARTSTOP)
    m_isAdjustRefreshRate = true;
}

void CHOOSER::IRepresentationChooser::OnUpdateScreenRes()
{
  const auto sInfo = CSrvBroker::GetResources().GetScreenInfo();

  LOG::Log(LOGINFO,
           "[Repr. chooser] Resolution set: %dx%d, max allowed: %dx%d, Adjust refresh rate: %i",
           sInfo.width, sInfo.height, sInfo.maxWidth, sInfo.maxHeight, m_isAdjustRefreshRate);

  // Use case: User chooses to upscale Kodi GUI from TV instead of Kodi engine.
  // In this case "Adjust refresh rate" setting can be enabled and then
  // the GUI resolution will be lower than the max allowed resolution.
  //
  // For example we can have the GUI at 1080p and when playback starts can be
  // auto-switched to 4k, but to allow Kodi do this we have to provide the
  // stream resolution that match the max allowed screen resolution.
  if (m_isAdjustRefreshRate && sInfo.width < sInfo.maxWidth && sInfo.height < sInfo.maxHeight)
  {
    m_screenCurrentWidth = sInfo.maxWidth;
    m_screenCurrentHeight = sInfo.maxHeight;
    m_isForceStartsMaxRes = true;
  }
  else
  {
    m_screenCurrentWidth = sInfo.width;
    m_screenCurrentHeight = sInfo.height;
  }
}

void CHOOSER::IRepresentationChooser::LogDetails(PLAYLIST::CRepresentation* currentRep,
                                                 PLAYLIST::CRepresentation* nextRep)
{
  if (!nextRep)
    return;

  if (!currentRep)
  {
    LOG::Log(LOGDEBUG,
             "[Repr. chooser] Selected representation\n"
             "ID %s (Bandwidth: %u bit/s, Resolution: %ix%i)",
             nextRep->GetId().data(), nextRep->GetBandwidth(), nextRep->GetWidth(),
             nextRep->GetHeight());
  }
  else if (currentRep != nextRep)
  {
    LOG::Log(LOGDEBUG,
             "[Repr. chooser] Changed representation\n"
             "Current ID %s (Bandwidth: %u bit/s, Resolution: %ix%i)\n"
             "Next ID %s (Bandwidth: %u bit/s, Resolution: %ix%i)",
             currentRep->GetId().data(), currentRep->GetBandwidth(), currentRep->GetWidth(),
             currentRep->GetHeight(), nextRep->GetId().data(), nextRep->GetBandwidth(),
             nextRep->GetWidth(), nextRep->GetHeight());
  }
}
