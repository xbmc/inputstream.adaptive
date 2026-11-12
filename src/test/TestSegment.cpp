/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "../common/Segment.h"

#include <gtest/gtest.h>

using namespace PLAYLIST;

namespace
{
CSegment MakeSeg(uint64_t start, uint64_t end)
{
  CSegment seg;
  seg.startPTS_ = start;
  seg.m_endPts = end;
  return seg;
}

// Contiguous timeline [0,10) [10,20) [20,30)
CSegContainer MakeContiguous()
{
  CSegContainer cont;
  cont.Add(MakeSeg(0, 10));
  cont.Add(MakeSeg(10, 20));
  cont.Add(MakeSeg(20, 30));
  return cont;
}
} // namespace

TEST(SegContainerTest, FindByPTSOrNextInsideSegment)
{
  const CSegContainer cont = MakeContiguous();
  ASSERT_NE(cont.FindByPTSOrNext(0), nullptr);
  EXPECT_EQ(cont.FindByPTSOrNext(0)->startPTS_, 0U);
  EXPECT_EQ(cont.FindByPTSOrNext(5)->startPTS_, 0U);
  EXPECT_EQ(cont.FindByPTSOrNext(15)->startPTS_, 10U);
  EXPECT_EQ(cont.FindByPTSOrNext(29)->startPTS_, 20U);
}

TEST(SegContainerTest, FindByPTSOrNextBoundarySelectsStartingSegment)
{
  const CSegContainer cont = MakeContiguous();
  ASSERT_NE(cont.FindByPTSOrNext(10), nullptr);
  EXPECT_EQ(cont.FindByPTSOrNext(10)->startPTS_, 10U);
  ASSERT_NE(cont.FindByPTSOrNext(20), nullptr);
  EXPECT_EQ(cont.FindByPTSOrNext(20)->startPTS_, 20U);
}

TEST(SegContainerTest, FindByPTSOrNextStreamEnd)
{
  const CSegContainer cont = MakeContiguous();
  ASSERT_NE(cont.FindByPTSOrNext(30), nullptr);
  EXPECT_EQ(cont.FindByPTSOrNext(30)->startPTS_, 20U);
  EXPECT_EQ(cont.FindByPTSOrNext(31), nullptr);
}

TEST(SegContainerTest, FindByPTSOrNextGap)
{
  // [0,10) gap [15,25)
  CSegContainer cont;
  cont.Add(MakeSeg(0, 10));
  cont.Add(MakeSeg(15, 25));
  ASSERT_NE(cont.FindByPTSOrNext(10), nullptr);
  EXPECT_EQ(cont.FindByPTSOrNext(10)->startPTS_, 15U);
  EXPECT_EQ(cont.FindByPTSOrNext(12)->startPTS_, 15U);
}

TEST(SegContainerTest, FindByPTSOrNextEmpty)
{
  const CSegContainer cont;
  EXPECT_EQ(cont.FindByPTSOrNext(0), nullptr);
}
