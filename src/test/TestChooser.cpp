/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "../common/ChooserDefault.h"

#include <gtest/gtest.h>

namespace
{
class TestRepresentationChooser : public CHOOSER::CRepresentationChooserDefault
{
public:
  void Configure(uint32_t initial, uint32_t minimum, uint32_t maximum)
  {
    m_bandwidthInitAuto = true;
    m_bandwidthInit = initial;
    m_bandwidthMin = minimum;
    m_bandwidthMax = maximum;
  }

  uint32_t CurrentBandwidth() const { return m_bandwidthCurrent; }
  uint32_t LimitedBandwidth() const { return m_bandwidthCurrentLimited; }
};
} // unnamed namespace

TEST(RepresentationChooserTest, IgnoresUnavailableDownloadSpeed)
{
  TestRepresentationChooser chooser;
  chooser.Configure(7000000, 4000000, 5000000);

  chooser.SetDownloadSpeed(0);
  chooser.PostInit();
  EXPECT_EQ(chooser.CurrentBandwidth(), 7000000U);
  EXPECT_EQ(chooser.LimitedBandwidth(), 5000000U);

  chooser.SetDownloadSpeed(0);
  EXPECT_EQ(chooser.CurrentBandwidth(), 7000000U);
  EXPECT_EQ(chooser.LimitedBandwidth(), 5000000U);
}
