/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AdaptiveTree.h"

namespace CHOOSER
{

constexpr bool BwCompare(const adaptive::AdaptiveTree::Representation* left,
                         const adaptive::AdaptiveTree::Representation* right)
{
  return left->bandwidth_ < right->bandwidth_;
};

class ATTR_DLL_LOCAL CRepresentationSelector
{
public:
  CRepresentationSelector(const int& resWidth, const int& resHeight);
  ~CRepresentationSelector() {}

  /*!
   * \brief Select the lowest representation (as index order)
   * \param adaptSet The adaption set
   * \return The lowest representation, otherwise nullptr if no available
   */
  adaptive::AdaptiveTree::Representation* Lowest(
      adaptive::AdaptiveTree::AdaptationSet* adaptSet) const;

  /*!
   * \brief Select the highest representation quality closer to the screen resolution
   * \param adaptSet The adaption set
   * \return The highest representation, otherwise nullptr if no available
   */
  adaptive::AdaptiveTree::Representation* Highest(
      adaptive::AdaptiveTree::AdaptationSet* adaptSet) const;

  /*!
   * \brief Select the representation with the higher bandwidth
   * \param adaptSet The adaption set
   * \return The representation with higher bandwidth, otherwise nullptr if no available
   */
  adaptive::AdaptiveTree::Representation* HighestBw(
      adaptive::AdaptiveTree::AdaptationSet* adaptSet) const;


  adaptive::AdaptiveTree::Representation* Higher(adaptive::AdaptiveTree::AdaptationSet* adaptSet,
                                                 adaptive::AdaptiveTree::Representation* currRep) const;

private:
  int m_screenWidth{0};
  int m_screenHeight{0};
};

} // namespace adaptive
