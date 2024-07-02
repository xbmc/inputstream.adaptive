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
#include <deque>
#include <string>
#include <string_view>
#include <vector>

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

class ATTR_DLL_LOCAL CSegContainer
{
public:
  CSegContainer() = default;
  ~CSegContainer() = default;

  /*!
   * \brief Get the segment pointer from the specified position
   * \param pos The position of segment
   * \return Segment pointer, otherwise nullptr if not found
   */
  const CSegment* Get(size_t pos) const;

  /*!
   * \brief Get the last segment pointer
   * \return Segment pointer, otherwise nullptr if not found
   */
  const CSegment* GetBack() const;

  /*!
   * \brief Get the first segment as pointer
   * \return Segment pointer, otherwise nullptr if not found
   */
  const CSegment* GetFront() const;

  /*!
   * \brief Get the next segment after the one specified.
   *        The search is done by number (if available) otherwise by PTS.
   * \return If found the segment pointer, otherwise nullptr.
   */
  const CSegment* GetNext(const CSegment* seg) const;

  /*!
   * \brief Try find same/similar segment in the timeline.
   *        The search is done by number (if available) otherwise by PTS.
   * \return If found the segment pointer, otherwise nullptr.
   */
  const CSegment* Find(const CSegment& seg) const;

  /*!
   * \brief Get index position of a segment pointer in the timeline.
   * \param elem The segment pointer to get the position
   * \return The index position, or SEGMENT_NO_POS if not found
   */
  const size_t GetPos(const CSegment* seg) const;

  /*!
   * \brief Add a segment to the container.
   * \param elem The segment to add
   */
  void Add(const CSegment& seg);

  /*!
   * \brief Append segment to the container, by increasing the count.
   * \param elem The segment to append
   */
  void Append(const CSegment& seg);

  void Swap(CSegContainer& other);

  /*!
   * \brief Delete all segments and clear the properties.
   */
  void Clear();

  bool IsEmpty() const { return m_segments.empty(); }

  /*!
   * \brief Get the number of the appended segments.
   * \return The number of appended segments.
   */
  size_t GetAppendCount() const { return m_appendCount; }

  /*!
   * \brief Get the number of segments.
   * \return The number of segments.
   */
  size_t GetSize() const { return m_segments.size(); }

  /*!
   * \brief Get the number of elements without taking into account those appended.
   * \return The number of elements.
   */
  size_t GetInitialSize() const { return m_segments.size() - m_appendCount; }

  /*!
   * \brief Get the duration of all segments.
   * \return The duration of all segments.
   */
  uint64_t GetDuration() const { return m_duration; }

  std::deque<CSegment>::const_iterator begin() const { return m_segments.begin(); }
  std::deque<CSegment>::const_iterator end() const { return m_segments.end(); }

private:
  // Has been used std::deque because there are uses of pointer references
  // deque container keeps memory addresses even if the container size increases (no reallocations)
  std::deque<CSegment> m_segments;
  size_t m_appendCount{0}; // Number of appended segments
  uint64_t m_duration{0}; // Sum of the duration of all segments
};

} // namespace PLAYLIST
