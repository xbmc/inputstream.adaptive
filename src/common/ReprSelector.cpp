/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ReprSelector.h"

#include <algorithm>
#include <vector>

using namespace CHOOSER;
using namespace PLAYLIST;

CRepresentationSelector::CRepresentationSelector(const int& resWidth, const int& resHeight)
{
  m_screenWidth = resWidth;
  m_screenHeight = resHeight;
}

PLAYLIST::CRepresentation* CRepresentationSelector::Lowest(PLAYLIST::CAdaptationSet* adaptSet) const
{
  auto& reps = adaptSet->GetRepresentations();
  return reps.empty() ? nullptr : reps[0].get();
}

PLAYLIST::CRepresentation* CRepresentationSelector::Highest(
    PLAYLIST::CAdaptationSet* adaptSet) const
{
  CRepresentation* highestRep{nullptr};

  for (auto& rep : adaptSet->GetRepresentations())
  {
    if (rep->GetWidth() <= m_screenWidth && rep->GetHeight() <= m_screenHeight)
    {
      if (!highestRep || (highestRep->GetWidth() <= rep->GetWidth() &&
                          highestRep->GetHeight() <= rep->GetHeight() &&
                          highestRep->GetBandwidth() < rep->GetBandwidth()))
      {
        highestRep = rep.get();
      }
    }
  }

  if (!highestRep)
    return Lowest(adaptSet);

  return highestRep;
}

PLAYLIST::CRepresentation* CRepresentationSelector::HighestBw(
    PLAYLIST::CAdaptationSet* adaptSet) const
{
  CRepresentation* repHigherBw{nullptr};

  for (auto& rep : adaptSet->GetRepresentations())
  {
    if (!repHigherBw || rep->GetBandwidth() > repHigherBw->GetBandwidth())
    {
      repHigherBw = rep.get();
    }
  }

  return repHigherBw;
}

PLAYLIST::CRepresentation* CRepresentationSelector::Higher(PLAYLIST::CAdaptationSet* adaptSet,
                                                           PLAYLIST::CRepresentation* currRep) const
{
  auto reps = adaptSet->GetRepresentationsPtr();
  auto repIt{
      std::upper_bound(reps.begin(), reps.end(), currRep, CRepresentation::CompareBandwidthPtr)};

  if (repIt == reps.end())
    return currRep;

  return *repIt;
}
