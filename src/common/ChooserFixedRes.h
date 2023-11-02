/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "Chooser.h"

#include <utility>

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

  void Initialize(const ADP::KODI_PROPS::ChooserProps& props) override;

  void PostInit() override;

  PLAYLIST::CRepresentation* GetNextRepresentation(PLAYLIST::CAdaptationSet* adp,
                                                   PLAYLIST::CRepresentation* currentRep) override;

private:
  std::pair<int, int> m_screenResMax; // Max resolution for non-protected video content
  std::pair<int, int> m_screenResSecureMax; // Max resolution for protected video content
};

} // namespace CHOOSER
