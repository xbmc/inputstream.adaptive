/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "Chooser.h"

namespace CHOOSER
{
/*!
 * \brief The quality of the stream is asked to the user by a dialog window
 */
class ATTR_DLL_LOCAL CRepresentationChooserAskQuality : public IRepresentationChooser
{
public:
  CRepresentationChooserAskQuality();
  ~CRepresentationChooserAskQuality() override {}

  void Initialize(const UTILS::PROPERTIES::ChooserProps& props) override;

  void PostInit() override;

  adaptive::AdaptiveTree::Representation* GetNextRepresentation(
      adaptive::AdaptiveTree::AdaptationSet* adp,
      adaptive::AdaptiveTree::Representation* currentRep) override;

private:
  bool m_isDialogShown{false};
  int m_selectedResWidth{0};
  int m_selectedResHeight{0};
};

} // namespace CHOOSER
