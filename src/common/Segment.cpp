/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Segment.h"
#include "utils/log.h"

using namespace PLAYLIST;

const CSegment* PLAYLIST::CSegContainer::Get(size_t pos) const
{
  if (pos == SEGMENT_NO_POS || m_segments.empty())
    return nullptr;

  if (pos >= m_segments.size())
  {
    LOG::LogF(LOGWARNING, "Position out-of-range (%zu of %zu)", pos, m_segments.size());
    return nullptr;
  }

  return &m_segments[pos];
}

const CSegment* PLAYLIST::CSegContainer::GetBack() const
{
  if (m_segments.empty())
    return nullptr;

  return &m_segments.back();
}

const CSegment* PLAYLIST::CSegContainer::GetFront() const
{
  if (m_segments.empty())
    return nullptr;

  return &m_segments.front();
}

const CSegment* PLAYLIST::CSegContainer::GetNext(const CSegment* seg) const
{
  if (!seg || seg->IsInitialization())
    return GetFront();

  // If available, find the segment by number, this is because some
  // live services provide inconsistent timestamps between manifest updates
  // which will make it ineffective to find the next segment
  if (seg->m_number != SEGMENT_NO_NUMBER)
  {
    const uint64_t number = seg->m_number;

    for (const CSegment& segment : m_segments)
    {
      if (segment.m_number > number)
        return &segment;
    }
  }
  else
  {
    const uint64_t startPTS = seg->startPTS_;

    for (const CSegment& segment : m_segments)
    {
      if (segment.startPTS_ > startPTS)
        return &segment;
    }
  }
  return nullptr;
}

const CSegment* PLAYLIST::CSegContainer::Find(const CSegment& seg) const
{
  // If available, find the segment by number, this is because some
  // live services provide inconsistent timestamps between manifest updates
  // which will make it ineffective to find the same segment
  if (seg.m_number != SEGMENT_NO_NUMBER)
  {
    const uint64_t number = seg.m_number;

    for (const CSegment& segment : m_segments)
    {
      if (segment.m_number == number)
        return &segment;
    }
  }
  else
  {
    const uint64_t startPTS = seg.startPTS_;

    for (const CSegment& segment : m_segments)
    {
      // Search by >= is intended to allow minimizing problems with encoders
      // that provide inconsistent timestamps between manifest updates
      if (segment.startPTS_ >= startPTS)
        return &segment;
    }
  }

  return nullptr;
}

const size_t PLAYLIST::CSegContainer::GetPos(const CSegment* seg) const
{
  for (size_t i = 0; i < m_segments.size(); ++i)
  {
    if (&m_segments[i] == seg)
      return i;
  }

  return SEGMENT_NO_POS;
}

void PLAYLIST::CSegContainer::Add(const CSegment& seg)
{
  m_duration += seg.m_endPts - seg.startPTS_;
  m_segments.emplace_back(seg);
}

void PLAYLIST::CSegContainer::Append(const CSegment& seg)
{
  m_duration += seg.m_endPts - seg.startPTS_;
  m_segments.emplace_back(seg);
  m_appendCount += 1;
}

void PLAYLIST::CSegContainer::Swap(CSegContainer& other)
{
  m_segments.swap(other.m_segments);
  std::swap(m_appendCount, other.m_appendCount);
  std::swap(m_duration, other.m_duration);
}

void PLAYLIST::CSegContainer::Clear()
{
  m_segments.clear();
  m_appendCount = 0;
  m_duration = 0;
}

