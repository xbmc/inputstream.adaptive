/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SegmentList.h"
#include "Segment.h"
#include "utils/log.h"

using namespace PLAYLIST;

PLAYLIST::CSegmentList::CSegmentList(const std::optional<CSegmentList>& other)
{
  if (other.has_value())
    *this = *other;
}

void PLAYLIST::CSegmentList::SetInitRange(std::string_view range)
{
  if (!ParseRangeRFC(range, m_initRangeBegin, m_initRangeEnd))
    LOG::LogF(LOGERROR, "Failed to parse \"range\" attribute");
}

CSegment PLAYLIST::CSegmentList::MakeInitSegment()
{
  CSegment seg;
  seg.SetIsInitialization(true);
  seg.startPTS_ = 0;
  seg.range_begin_ = m_initRangeBegin;
  seg.range_end_ = m_initRangeEnd;
  seg.url = m_initSourceUrl;
  return seg;
}
