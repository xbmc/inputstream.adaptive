/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SegmentBase.h"

#include "AdaptiveUtils.h"
#include "Segment.h"
#include "utils/log.h"

using namespace PLAYLIST;

void PLAYLIST::CSegmentBase::SetIndexRange(std::string_view indexRange)
{
  if (!ParseRangeRFC(indexRange, m_indexRangeBegin, m_indexRangeEnd))
    LOG::LogF(LOGERROR, "Failed to parse \"indexrange\" attribute");
}

void PLAYLIST::CSegmentBase::SetInitRange(std::string_view range)
{
  if (!ParseRangeRFC(range, m_initRangeBegin, m_initRangeEnd))
    LOG::LogF(LOGERROR, "Failed to parse initialization \"range\" attribute");
}

CSegment PLAYLIST::CSegmentBase::MakeIndexSegment()
{
  CSegment seg;
  seg.range_begin_ = m_indexRangeBegin;
  seg.range_end_ = m_indexRangeEnd;
  return seg;
}

CSegment PLAYLIST::CSegmentBase::MakeInitSegment()
{
  CSegment seg;
  seg.SetIsInitialization(true);
  seg.startPTS_ = 0;
  if (HasInitialization())
  {
    seg.range_begin_ = m_initRangeBegin;
    seg.range_end_ = m_initRangeEnd;
  }
  else
    LOG::LogF(LOGWARNING,
              "The \"range\" attribute is missing in the SegmentBase initialization tag");

  return seg;
}
