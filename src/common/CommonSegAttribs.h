/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "SegTemplate.h"
#include "SegmentList.h"

#include <optional>

namespace PLAYLIST
{
// This class provide common place for shared members/methods
// with the possibility to retrieve the value from the parent class, when needed.
class ATTR_DLL_LOCAL CCommonSegAttribs
{
public:
  CCommonSegAttribs(CCommonSegAttribs* parent = nullptr);
  virtual ~CCommonSegAttribs() {}

  std::optional<CSegmentList>& GetSegmentList() { return m_segmentList; }
  void SetSegmentList(const CSegmentList& segmentList) { m_segmentList = segmentList; }
  bool HasSegmentList() { return m_segmentList.has_value(); }

  std::optional<CSegmentTemplate>& GetSegmentTemplate() { return m_segmentTemplate; }
  std::optional<CSegmentTemplate> GetSegmentTemplate() const { return m_segmentTemplate; }
  void SetSegmentTemplate(const CSegmentTemplate& segTemplate) { m_segmentTemplate = segTemplate; }
  bool HasSegmentTemplate() const { return m_segmentTemplate.has_value(); }

  /*!
   * \brief Get the optional segment end number. Use HasSegmentEndNr method to know if the value is set.
   * \return The segment end number or the default value (0).
   */
  uint64_t GetSegmentEndNr();
  void SetSegmentEndNr(const uint64_t segNumber) { m_segEndNr = segNumber; }
  bool HasSegmentEndNr();

  uint64_t GetStartPTS() const;
  void SetStartPTS(uint64_t startPts) { m_startPts = startPts; }

protected:
  CCommonSegAttribs* m_parentCommonSegAttribs{nullptr};
  std::optional<CSegmentList> m_segmentList;
  std::optional<CSegmentTemplate> m_segmentTemplate;
  std::optional<uint64_t> m_segEndNr;
  uint64_t m_startPts{0};
};

} // namespace PLAYLIST
