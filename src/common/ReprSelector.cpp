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
using namespace adaptive;

CRepresentationSelector::CRepresentationSelector(const int& resWidth, const int& resHeight)
{
  m_screenWidth = resWidth;
  m_screenHeight = resHeight;
}

AdaptiveTree::Representation* CRepresentationSelector::Lowest(
    AdaptiveTree::AdaptationSet* adaptSet) const
{
  const std::vector<AdaptiveTree::Representation*>& reps = adaptSet->representations_;
  return reps.empty() ? nullptr : *reps.begin();
}

AdaptiveTree::Representation* CRepresentationSelector::Highest(AdaptiveTree::AdaptationSet* adaptSet) const
{
  AdaptiveTree::Representation* highestRep{nullptr};

  for (auto rep : adaptSet->representations_)
  {
    if (!rep)
      continue;

    if (rep->width_ <= m_screenWidth && rep->height_ <= m_screenHeight)
    {
      if (!highestRep ||
          (highestRep->width_ <= rep->width_ && highestRep->height_ <= rep->height_ &&
           highestRep->bandwidth_ < rep->bandwidth_))
      {
        highestRep = rep;
      }
    }
  }

  if (!highestRep)
    return Lowest(adaptSet);

  return highestRep;
}

AdaptiveTree::Representation* CRepresentationSelector::HighestBw(
    AdaptiveTree::AdaptationSet* adaptSet) const
{
  AdaptiveTree::Representation* repHigherBw{nullptr};

  for (auto rep : adaptSet->representations_)
  {
    if (!rep)
      continue;

    if (!repHigherBw || rep->bandwidth_ > repHigherBw->bandwidth_)
    {
      repHigherBw = rep;
    }
  }

  return repHigherBw;
}

AdaptiveTree::Representation* CRepresentationSelector::Higher(
    AdaptiveTree::AdaptationSet* adaptSet, AdaptiveTree::Representation* currRep) const
{
  const std::vector<AdaptiveTree::Representation*>& reps{adaptSet->representations_};
  auto repIt{std::upper_bound(reps.cbegin(), reps.cend(), currRep, BwCompare)};

  if (repIt == reps.cend())
    return currRep;

  return *repIt;
}
