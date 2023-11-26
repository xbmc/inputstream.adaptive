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

  /*!
   * \brief Get the optional segment end number. Use HasSegmentEndNr method to know if the value is set.
   * \return The segment end number or the default value (0).
   */
  uint64_t GetSegmentEndNr();
  void SetSegmentEndNr(const uint64_t segNumber) { m_segEndNr = segNumber; }
  bool HasSegmentEndNr();

protected:
  CCommonSegAttribs* m_parentCommonSegAttribs{nullptr};
  std::optional<CSegmentList> m_segmentList;
  std::optional<uint64_t> m_segEndNr;
};

} // namespace PLAYLIST
