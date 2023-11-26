/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CommonSegAttribs.h"

using namespace PLAYLIST;

PLAYLIST::CCommonSegAttribs::CCommonSegAttribs(CCommonSegAttribs* parent /* = nullptr */)
{
  m_parentCommonSegAttribs = parent;
}

std::optional<CSegmentList>& PLAYLIST::CCommonSegAttribs::GetSegmentList()
{
  if (m_segmentList.has_value())
    return m_segmentList;
  if (m_parentCommonSegAttribs && m_parentCommonSegAttribs->m_segmentList.has_value())
    return m_parentCommonSegAttribs->m_segmentList;

  return m_segmentList; // Empty data
}

bool PLAYLIST::CCommonSegAttribs::HasSegmentList()
{
  return m_segmentList.has_value() ||
         (m_parentCommonSegAttribs && m_parentCommonSegAttribs->m_segmentList.has_value());
}

uint64_t PLAYLIST::CCommonSegAttribs::GetSegmentEndNr()
{
  if (m_segEndNr.has_value())
    return *m_segEndNr;

  if (m_parentCommonSegAttribs)
    return m_parentCommonSegAttribs->GetSegmentEndNr();

  return 0; // Default value
}

bool PLAYLIST::CCommonSegAttribs::HasSegmentEndNr()
{
  return m_segEndNr.has_value() ||
         (m_parentCommonSegAttribs && m_parentCommonSegAttribs->HasSegmentEndNr());
}
