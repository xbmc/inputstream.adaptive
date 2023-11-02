/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ChooserFixedRes.h"

#include "AdaptationSet.h"
#include "CompKodiProps.h"
#include "CompSettings.h"
#include "ReprSelector.h"
#include "Representation.h"
#include "SrvBroker.h"
#include "utils/log.h"

using namespace CHOOSER;
using namespace PLAYLIST;

CRepresentationChooserFixedRes::CRepresentationChooserFixedRes()
{
  LOG::Log(LOGDEBUG, "[Repr. chooser] Type: Fixed resolution");
}

void CRepresentationChooserFixedRes::Initialize(const ADP::KODI_PROPS::ChooserProps& props)
{
  auto& settings = CSrvBroker::GetSettings();

  m_screenResMax = settings.GetResMax();
  m_screenResSecureMax = settings.GetResSecureMax();

  // Override settings with Kodi/video add-on properties

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
           "Resolution max for secure decoder: %ix%i",
           m_screenResMax.first, m_screenResMax.second, m_screenResSecureMax.first,
           m_screenResSecureMax.second);
}

void CRepresentationChooserFixedRes::PostInit()
{
  LOG::Log(LOGDEBUG,
           "[Repr. chooser] Stream selection conditions\n"
           "Screen resolution: %ix%i",
           m_screenCurrentWidth, m_screenCurrentHeight);
}

PLAYLIST::CRepresentation* CRepresentationChooserFixedRes::GetNextRepresentation(
    PLAYLIST::CAdaptationSet* adp, PLAYLIST::CRepresentation* currentRep)
{
  if (currentRep)
    return currentRep;

  std::pair<int, int> resolution{m_isSecureSession ? m_screenResSecureMax : m_screenResMax};

  if (resolution.first == 0) // Max limit set to "Auto"
    resolution = {m_screenCurrentWidth, m_screenCurrentHeight};

  CRepresentationSelector selector{resolution.first, resolution.second};

  if (adp->GetStreamType() == StreamType::VIDEO)
  {
    CRepresentation* selRep{selector.Highest(adp)};
    LogDetails(nullptr, selRep);
    return selRep;
  }
  else
  {
    return selector.HighestBw(adp);
  }
}
