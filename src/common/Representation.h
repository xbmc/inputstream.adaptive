/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AdaptationSet.h"
#include "AdaptiveUtils.h"
#include "CommonAttribs.h"
#include "Segment.h"
#include "SegmentBase.h"
#include "SegTemplate.h"
#include "SegmentList.h"

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <set>

namespace PLAYLIST
{

class ATTR_DLL_LOCAL CRepresentation : public CCommonSegAttribs, public CCommonAttribs
{
public:
  CRepresentation(CAdaptationSet* parent = nullptr)
    : CCommonSegAttribs(parent), CCommonAttribs(parent)
  {
  }
  ~CRepresentation() {}

  /*!
   * \brief Set the parent AdaptationSet class, it may be necessary to allow methods to obtain
   *        the data of some common attributes from the parent when the representation is missing data.
   *        To be used if you plan to set or move a representation to an AdptationSet or a different one.
   * \param parent[OPT] The parent AdaptationSet to link
   */
  void SetParent(CAdaptationSet* parent = nullptr, bool copyData = false);

  // Share AdaptationSet common attribs
  static std::unique_ptr<CRepresentation> MakeUniquePtr(CAdaptationSet* parent = nullptr)
  {
    return std::make_unique<CRepresentation>(parent);
  }

  std::string_view GetId() const { return m_id; }
  void SetId(std::string_view id) { m_id = id; }

  std::string GetSourceUrl() const { return m_sourceUrl; }
  void SetSourceUrl(std::string_view sourceUrl) { m_sourceUrl = sourceUrl; }

  std::string GetBaseUrl() const { return m_baseUrl; }
  void SetBaseUrl(std::string_view baseUrl) { m_baseUrl = baseUrl; }

  /*!
   * \brief Add a codec string
   */
  void AddCodecs(std::string_view codecs);

  /*!
   * \brief Add codec string
   */
  void AddCodecs(const std::set<std::string>& codecs);

  /*!
   * \brief Get codec list, a common rule for a codec string among manifest types is the use
   *        of fourcc codes, but codec string can contains other info as ISO BMFF (RFC 6381) format
   */
  std::set<std::string> GetCodecs() const { return m_codecs; }

  /*!
   * \brief Get codec list, a common rule for a codec string among manifest types is the use
   *        of fourcc codes, but codec string can contains other info as ISO BMFF (RFC 6381) format
   */
  const std::set<std::string>& GetCodecs() { return m_codecs; }

  // ISA custom attribute
  const std::vector<uint8_t>& GetCodecPrivateData() const { return m_codecPrivateData; }
  void SetCodecPrivateData(const std::vector<uint8_t>& codecPrivateData)
  {
    m_codecPrivateData = codecPrivateData;
  }

  uint32_t GetBandwidth() const { return m_bandwidth; }
  void SetBandwidth(uint32_t bandwidth) { m_bandwidth = bandwidth; }

  uint16_t GetHdcpVersion() const { return m_hdcpVersion; }
  void SetHdcpVersion(uint16_t hdcpVersion) { m_hdcpVersion = hdcpVersion; }

  /*!
   * \brief The segment timeline.
   */
  CSegContainer& Timeline() { return m_segmentTimeline; }
  /*!
   * \brief The segment timeline.
   */
  const CSegContainer& Timeline() const { return m_segmentTimeline; }

  std::optional<CSegmentBase>& GetSegmentBase() { return m_segmentBase; }
  void SetSegmentBase(const CSegmentBase& segBase) { m_segmentBase = segBase; }
  bool HasSegmentBase() const { return m_segmentBase.has_value(); }

  uint64_t GetStartNumber() const { return m_startNumber; }
  void SetStartNumber(uint64_t startNumber) { m_startNumber = startNumber; }

  /*!
   * \brief Get the duration, in timescale units.
   * \return The duration value.
   */
  uint64_t GetDuration() const { return m_duration; }

  /*!
   * \brief Set the duration, in timescale units.
   */
  void SetDuration(uint64_t duration) { m_duration = duration; }

  /*!
   * \brief Get the timescale unit.
   * \return The timescale unit, otherwise 0 if not set.
   */
  uint32_t GetTimescale() const { return m_timescale; }

  /*!
   * \brief Set the timescale unit.
   */
  void SetTimescale(uint32_t timescale) { m_timescale = timescale; }

  /*!
   * \brief Determines if the representation contains a single "sidecar" file subtitle,
   *        used for the entire duration of the video.
   * \return True if subtitles are as single file, otherwise false
   */
  bool IsSubtitleFileStream() const { return m_isSubtitleFileStream; }
  void SetIsSubtitleFileStream(bool isSubtitleFileStream)
  {
    m_isSubtitleFileStream = isSubtitleFileStream;
  }

  bool IsEnabled() const { return m_isEnabled; }
  void SetIsEnabled(bool isEnabled) { m_isEnabled = isEnabled; }

  bool IsWaitForSegment() const { return m_isWaitForSegment; }
  void SetIsWaitForSegment(bool isWaitForSegment) { m_isWaitForSegment = isWaitForSegment; }

  // Define if it is a dummy representation for audio stream, that is embedded on the video stream
  bool IsIncludedStream() const { return m_isIncludedStream; }
  void SetIsIncludedStream(bool isIncludedStream) { m_isIncludedStream = isIncludedStream; }

  void CopyHLSData(const CRepresentation* other);

  static bool CompareBandwidth(std::unique_ptr<CRepresentation>& left,
                               std::unique_ptr<CRepresentation>& right)
  {
    return left->m_bandwidth < right->m_bandwidth;
  }

  static bool CompareBandwidthPtr(CRepresentation* left, CRepresentation* right)
  {
    return left->m_bandwidth < right->m_bandwidth;
  }

  uint16_t m_psshSetPos{PSSHSET_POS_DEFAULT}; // Index position of the PSSHSet
  const uint16_t GetPsshSetPos() const { return m_psshSetPos; }

  size_t expired_segments_{0};

  const CSegment* current_segment_{nullptr};

  bool HasInitSegment() const { return m_initSegment.has_value(); }
  void SetInitSegment(CSegment initSegment) { m_initSegment = initSegment; }
  std::optional<CSegment>& GetInitSegment() { return m_initSegment; }

  /*!
   * \brief Get segment following the current one.
   * \return If found the segment pointer, otherwise nullptr.
   */
  const CSegment* GetNextSegment();

  /*!
   * \brief Get the segment number of current segment.
   * \return If found the number, otherwise SEGMENT_NO_NUMBER.
   */
  const uint64_t GetCurrentSegNumber() const;

  /*!
   * \brief Get the segment number of specified segment.
   * \return If found the number, otherwise SEGMENT_NO_NUMBER.
   */
  const uint64_t GetSegNumber(const CSegment* seg) const;

  uint32_t timescale_ext_{0};
  uint32_t timescale_int_{0};

  void SetScaling();

  std::chrono::time_point<std::chrono::system_clock> repLastUpdated_;

  //! @todo: to be reworked or deleted
  uint32_t assured_buffer_duration_{0};
  uint32_t max_buffer_duration_{0};

protected:
  std::string m_id;
  std::string m_sourceUrl;
  std::string m_baseUrl;

  std::set<std::string> m_codecs;
  std::vector<uint8_t> m_codecPrivateData;

  uint32_t m_bandwidth{0}; // as bit/s

  uint16_t m_hdcpVersion{0}; // 0 if not set

  std::optional<CSegmentBase> m_segmentBase;
  std::optional<CSegment> m_initSegment;

  uint64_t m_startNumber{1};

  CSegContainer m_segmentTimeline;

  uint64_t m_duration{0};
  uint32_t m_timescale{0};

  bool m_isSubtitleFileStream{false};

  bool m_isEnabled{false};
  bool m_isWaitForSegment{false};

  bool m_isIncludedStream{false};
};

} // namespace PLAYLIST
