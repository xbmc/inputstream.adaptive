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
#include "../test/KodiStubs.h"
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

  static constexpr uint64_t NO_RANGE_VALUE = std::numeric_limits<uint64_t>::max();

  //! @todo: create getters/setters
  //! its possible add a way to determinate if range is set

  //Either byterange start or timestamp or NO_RANGE_VALUE
  uint64_t range_begin_ = NO_RANGE_VALUE;
  //Either byterange end or sequence_id if range_begin is NO_RANGE_VALUE
  uint64_t range_end_ = NO_RANGE_VALUE;
  std::string url;
  uint64_t startPTS_ = NO_PTS_VALUE;
  uint64_t m_duration = 0; // If available gives the media duration of a segment (depends on type of stream e.g. HLS)
  uint16_t pssh_set_ = PSSHSET_POS_DEFAULT;

  void Copy(const CSegment* src);
};

} // namespace adaptive
