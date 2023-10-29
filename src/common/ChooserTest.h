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
   * \brief Test for stream switching cases
   */
  class ATTR_DLL_LOCAL CRepresentationChooserTest : public IRepresentationChooser
  {
  public:
    CRepresentationChooserTest();
    ~CRepresentationChooserTest() override {}

    virtual void Initialize(const ADP::KODI_PROPS::ChooserProps& props) override;

    virtual void PostInit() override;

    virtual StreamSelection GetStreamSelectionMode() override { return m_streamSelectionMode; }

    PLAYLIST::CRepresentation* GetNextRepresentation(
        PLAYLIST::CAdaptationSet* adp, PLAYLIST::CRepresentation* currentRep) override;

  protected:
    enum class TestMode
    {
      NONE = 0,
      SWITCH_SEGMENTS
    };

    TestMode m_testMode{TestMode::NONE};
    StreamSelection m_streamSelectionMode = StreamSelection::AUTO;
    int m_segmentsElapsed{1};
    int m_segmentsLimit{1};
  };

} // namespace CHOOSER
