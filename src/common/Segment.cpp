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

CSegment* PLAYLIST::CSegContainer::Get(size_t pos)
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

CSegment* PLAYLIST::CSegContainer::GetBack()
{
  if (m_segments.empty())
    return nullptr;

  return &m_segments.back();
}

CSegment* PLAYLIST::CSegContainer::GetFront()
{
  if (m_segments.empty())
    return nullptr;

  return &m_segments.front();
}

const size_t PLAYLIST::CSegContainer::GetPosition(const CSegment* elem) const
{
  for (size_t i = 0; i < m_segments.size(); ++i)
  {
    if (&m_segments[i] == elem)
      return i;
  }

  return SEGMENT_NO_POS;
}

void PLAYLIST::CSegContainer::Add(const CSegment& elem)
{
  m_duration += elem.m_endPts - elem.startPTS_;
  m_segments.emplace_back(elem);
}

void PLAYLIST::CSegContainer::Append(const CSegment& elem)
{
  m_duration += elem.m_endPts - elem.startPTS_;
  m_segments.emplace_back(elem);
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

