/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

#include <optional>
#include <string>
#include <string_view>

namespace PLAYLIST
{
// Forward
class CSegment;

// SegmentTemplate class provide segment template data
// of class itself or when not set of the parent class (if any).
class ATTR_DLL_LOCAL CSegmentTemplate
{
public:
  CSegmentTemplate() = default;
  CSegmentTemplate(const std::optional<CSegmentTemplate>& other);
  ~CSegmentTemplate() = default;

  std::string GetInitialization() const;
  void SetInitialization(std::string_view init) { m_initialization = init; }

  bool HasInitialization() const { return !GetInitialization().empty(); }

  std::string GetMedia() const;
  void SetMedia(std::string_view media) { m_media = media; }

  uint32_t GetTimescale() const;
  void SetTimescale(uint32_t timescale) { m_timescale = timescale; }

  /*!
   * \brief Get the duration, in timescale unit.
   * \return The duration value.
   */
  uint32_t GetDuration() const;
  void SetDuration(uint32_t duration) { m_duration = duration; }

  uint64_t GetStartNumber() const;
  void SetStartNumber(uint64_t startNumber) { m_startNumber = startNumber; }

  /*!
   * \brief Get the optional segment end number. Use HasEndNumber method to know if the value is set.
   * \return The segment end number or the default value (0).
   */
  uint64_t GetEndNumber() const;
  void SetEndNumber(uint64_t endNumber) { m_endNumber = endNumber; }
  bool HasEndNumber() const { return m_endNumber.has_value(); }

  uint64_t GetPresTimeOffset() const;
  void SetPresTimeOffset(uint64_t ptsOffset) { m_ptsOffset = ptsOffset; }
  bool HasPresTimeOffset() const { return m_ptsOffset.has_value(); }

  // \brief Defines a <SegmentTimeline>, <S> element
  struct TimelineElement
  {
    uint64_t time{0};
    uint32_t duration{0};
    uint32_t repeat{0};
  };

  std::vector<TimelineElement>& Timeline() { return m_timeline; }
  const std::vector<TimelineElement>& Timeline() const { return m_timeline; }
  bool HasTimeline() const { return !m_timeline.empty(); }

  CSegment MakeInitSegment();

  std::string FormatUrl(std::string_view url,
                        const std::string id,
                        const uint32_t bandwidth,
                        const uint64_t number,
                        const uint64_t time);

private:
  std::string FormatIdentifier(std::string_view identifier, const uint64_t value);

  std::string m_initialization;
  std::string m_media;
  std::optional<uint32_t> m_timescale;
  std::optional<uint32_t> m_duration;
  std::optional<uint64_t> m_startNumber;
  std::optional<uint64_t> m_endNumber;
  std::optional <uint64_t> m_ptsOffset;
  std::vector<TimelineElement> m_timeline;
};

} // namespace PLAYLIST
