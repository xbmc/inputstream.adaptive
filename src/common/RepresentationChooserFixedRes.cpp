/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RepresentationChooserFixedRes.h"

#include "../utils/SettingsUtils.h"
#include "../utils/log.h"
#include "RepresentationSelector.h"

using namespace adaptive;
using namespace CHOOSER;
using namespace UTILS;

CRepresentationChooserFixedRes::CRepresentationChooserFixedRes()
{
  LOG::Log(LOGDEBUG, "[Repr. chooser] Type: Fixed resolution");
}

void CRepresentationChooserFixedRes::Initialize(const UTILS::PROPERTIES::ChooserProps& props)
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

AdaptiveTree::Representation* CRepresentationChooserFixedRes::ChooseRepresentation(
    AdaptiveTree::AdaptationSet* adp)
{
  std::pair<int, int> resolution{m_isSecureSession ? m_screenResSecureMax : m_screenResMax};

  if (resolution.first == 0) // Max limit set to "Auto"
    resolution = {m_screenCurrentWidth, m_screenCurrentHeight};

  CRepresentationSelector selector{resolution.first, resolution.second};

  if (adp->type_ == AdaptiveTree::VIDEO)
    return selector.Highest(adp);
  else
    return selector.HighestBw(adp);
}

AdaptiveTree::Representation* CRepresentationChooserFixedRes::ChooseNextRepresentation(
    AdaptiveTree::AdaptationSet* adp, AdaptiveTree::Representation* currentRep)
{
  return currentRep;
}
