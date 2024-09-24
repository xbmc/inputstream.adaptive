/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AdaptiveUtils.h"

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

#include <algorithm>
#include <string>
#include <string_view>

namespace PLAYLIST
{
class ATTR_DLL_LOCAL CSegment
{
public:
  CSegment() {}
  ~CSegment(){}

  //! @todo: create getters/setters

  // Byte range start
  uint64_t range_begin_ = NO_VALUE;
  // Byte range end
  uint64_t range_end_ = NO_VALUE;
  std::string url;

  uint64_t startPTS_ = NO_PTS_VALUE; // The start PTS, in timescale units
  uint64_t m_endPts = NO_PTS_VALUE; // The end PTS, in timescale units
  uint16_t pssh_set_ = PSSHSET_POS_DEFAULT;

  uint64_t m_time{0}; // Timestamp
  uint64_t m_number{SEGMENT_NO_NUMBER};

  /*!
   * \brief Determines if it is an initialization segment.
   * \return True if it is an initialization segment, otherwise false for media segment.
   */
  bool IsInitialization() const { return m_isInitialization; }
  void SetIsInitialization(bool isInitialization) { m_isInitialization = isInitialization; }

  /*!
   * \brief Determines if there is a byte range set.
   * \return True if there is a byte range, otherwise false.
   */
  bool HasByteRange() const { return range_begin_ != NO_VALUE || range_end_ != NO_VALUE; }

private:
  bool m_isInitialization{false};
};

} // namespace PLAYLIST
