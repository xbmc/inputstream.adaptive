/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SegmentList.h"

using namespace PLAYLIST;

uint64_t PLAYLIST::CSegmentList::GetStartNumber() const
{
  if (m_startNumber > 0 || !m_parentSegList)
    return m_startNumber;
  return m_parentSegList->GetStartNumber();
}

uint64_t PLAYLIST::CSegmentList::GetDuration() const
{
  if (m_duration > 0 || !m_parentSegList)
    return m_duration;
  return m_parentSegList->GetDuration();
}

uint32_t PLAYLIST::CSegmentList::GetTimescale() const
{
  if (m_timescale > 0 || !m_parentSegList)
    return m_timescale;
  return m_parentSegList->GetTimescale();
}

uint64_t PLAYLIST::CSegmentList::GetPresTimeOffset() const
{
  if (m_ptsOffset > 0 || !m_parentSegList)
    return m_ptsOffset;
  return m_parentSegList->GetPresTimeOffset();
}
