/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "SegmentList.h"

#include <optional>

namespace PLAYLIST
{
// CCommonSegAttribs class provide attribute data
// of class itself or when not set of the parent class (if any).
class ATTR_DLL_LOCAL CCommonSegAttribs
{
public:
  CCommonSegAttribs(CCommonSegAttribs* parent = nullptr);
  virtual ~CCommonSegAttribs() {}

  std::optional<CSegmentList>& GetSegmentList();
  void SetSegmentList(const CSegmentList& segmentList) { m_segmentList = segmentList; }
  bool HasSegmentList();

protected:
  std::optional<CSegmentList> m_segmentList;

private:
  CCommonSegAttribs* m_parentCommonSegAttribs{nullptr};
};

} // namespace PLAYLIST
