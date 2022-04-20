/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "RepresentationChooser.h"

namespace CHOOSER
{
/*!
 * \brief The stream quality is fixed to the max available resolution
 */
class ATTR_DLL_LOCAL CRepresentationChooserFixedRes : public IRepresentationChooser
{
public:
  CRepresentationChooserFixedRes();
  ~CRepresentationChooserFixedRes() override {}

  virtual void Initialize(const UTILS::PROPERTIES::ChooserProps& props) override;

  virtual void PostInit() override;

  adaptive::AdaptiveTree::Representation* ChooseRepresentation(
      adaptive::AdaptiveTree::AdaptationSet* adp) override;

  adaptive::AdaptiveTree::Representation* ChooseNextRepresentation(
      adaptive::AdaptiveTree::AdaptationSet* adp,
      adaptive::AdaptiveTree::Representation* currentRep) override;

protected:
  std::pair<int, int> m_screenResMax; // Max resolution for non-protected video content
  std::pair<int, int> m_screenResSecureMax; // Max resolution for protected video content
};

} // namespace CHOOSER
