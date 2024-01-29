/*
 *  Copyright (C) 2021 Team Kodi
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
 * \brief The quality of the streams is fixed and can be changed
 *        through Kodi OSD settings.
 */
class ATTR_DLL_LOCAL CRepresentationChooserManualOSD : public IRepresentationChooser
{
public:
  CRepresentationChooserManualOSD();
  ~CRepresentationChooserManualOSD() override {}

  virtual void Initialize(const ADP::KODI_PROPS::ChooserProps& props) override;
  virtual void SetSecureSession(const bool isSecureSession) override;
  virtual void PostInit() override;

  virtual StreamSelection GetStreamSelectionMode() override { return m_streamSelectionMode; }

  PLAYLIST::CRepresentation* GetNextRepresentation(PLAYLIST::CAdaptationSet* adp,
                                                   PLAYLIST::CRepresentation* currentRep) override;

protected:
  void RefreshResolution();

  StreamSelection m_streamSelectionMode = StreamSelection::AUTO;

  int m_screenWidth{0};
  int m_screenHeight{0};

  std::pair<int, int> m_screenResMax; // Max resolution for non-protected video content
  std::pair<int, int> m_screenResSecureMax; // Max resolution for protected video content
};

} // namespace CHOOSER
