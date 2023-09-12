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

#include <string_view>

namespace PLAYLIST
{
// Forward
class CSegment;

class ATTR_DLL_LOCAL CSegmentBase
{
public:
  CSegmentBase() = default;
  ~CSegmentBase() = default;

  void SetIndexRange(std::string_view indexRange);
  void SetInitRange(std::string_view range);

  void SetIndexRangeBegin(uint64_t value) { m_indexRangeBegin = value; }
  void SetIndexRangeEnd(uint64_t value) { m_indexRangeEnd = value; }

  uint64_t GetIndexRangeBegin() { return m_indexRangeBegin; }
  uint64_t GetIndexRangeEnd() { return m_indexRangeEnd; }

  void SetIsRangeExact(bool isRangeExact) { m_isRangeExact = isRangeExact; }

  void SetTimescale(uint32_t timescale) { m_timescale = timescale; }
  uint32_t GetTimescale() { return m_timescale; }

  bool HasInitialization() { return m_initRangeBegin != NO_VALUE && m_initRangeEnd != NO_VALUE; }

  CSegment MakeIndexSegment();
  CSegment MakeInitSegment();

private:
  uint64_t m_indexRangeBegin{0};
  uint64_t m_indexRangeEnd{0};

  uint64_t m_initRangeBegin{NO_VALUE};
  uint64_t m_initRangeEnd{NO_VALUE};
  
  uint32_t m_timescale{0};
  bool m_isRangeExact{false};
};

} // namespace PLAYLIST
