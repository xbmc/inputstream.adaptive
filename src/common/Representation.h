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
#include "../test/KodiStubs.h"
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
  void SetParent(CAdaptationSet* parent = nullptr)
  {
    CCommonSegAttribs::m_parentCommonSegAttribs = parent;
    CCommonAttribs::m_parentCommonAttributes = parent;
  }

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

  void AddCodecs(std::string_view codecs);
  void AddCodecs(const std::set<std::string>& codecs);
  std::set<std::string> GetCodecs() const { return m_codecs; }
  const std::set<std::string>& GetCodecs() { return m_codecs; }
  std::string GetFirstCodec() const { return m_codecs.empty() ? "" : *m_codecs.begin(); }

  // Check if a codec exists, convenient function to check within of strings
  // e.g find "ttml" return true also when there is a "stpp.ttml.im1t" codec name
  bool ContainsCodec(std::string_view codec) const;
  bool ContainsCodec(std::string_view codec, std::string& codecStr) const;

  // ISA custom attribute
  std::string& GetCodecPrivateData() { return m_codecPrivateData; }
  void SetCodecPrivateData(std::string_view codecPrivateData)
  {
    m_codecPrivateData = codecPrivateData;
  }

  uint32_t GetBandwidth() const { return m_bandwidth; }
  void SetBandwidth(uint32_t bandwidth) { m_bandwidth = bandwidth; }

  uint16_t GetHdcpVersion() const { return m_hdcpVersion; }
  void SetHdcpVersion(uint16_t hdcpVersion) { m_hdcpVersion = hdcpVersion; }

  CSpinCache<CSegment>& SegmentTimeline() { return m_segmentTimeline; }
  CSpinCache<CSegment> SegmentTimeline() const { return m_segmentTimeline; }
  bool HasSegmentTimeline() { return !m_segmentTimeline.IsEmpty(); }

  std::optional<CSegmentTemplate>& GetSegmentTemplate() { return m_segmentTemplate; }
  std::optional<CSegmentTemplate> GetSegmentTemplate() const { return m_segmentTemplate; }
  void SetSegmentTemplate(const CSegmentTemplate& segTemplate) { m_segmentTemplate = segTemplate; }
  bool HasSegmentTemplate() const { return m_segmentTemplate.has_value(); }

  std::optional<CSegmentBase>& GetSegmentBase() { return m_segmentBase; }
  void SetSegmentBase(const CSegmentBase& segBase) { m_segmentBase = segBase; }
  bool HasSegmentBase() const { return m_segmentBase.has_value(); }

  uint32_t GetTimescale() const { return m_timescale; }
  void SetTimescale(uint32_t timescale) { m_timescale = timescale; }

  uint64_t GetStartNumber() const { return m_startNumber; }
  void SetStartNumber(uint64_t startNumber) { m_startNumber = startNumber; }

  uint64_t GetDuration() const { return m_duration; }
  void SetDuration(uint64_t duration) { m_duration = duration; }

  /*!
   * \brief Determines when the representation contains subtitles as single file
   *        for the entire duration of the video.
   * \return True if subtitles are as single file, otherwise false
   */
  bool IsSubtitleFileStream() const { return m_isSubtitleFileStream; }
  void SetIsSubtitleFileStream(bool isSubtitleFileStream)
  {
    m_isSubtitleFileStream = isSubtitleFileStream;
  }

  bool HasInitPrefixed() const { return m_hasInitPrefixed; }
  void SetHasInitPrefixed(bool hasInitPrefixed) { m_hasInitPrefixed = hasInitPrefixed; }

  bool HasSegmentsUrl() const { return m_hasSegmentsUrl; }
  void SetHasSegmentsUrl(bool hasSegmentsUrl) { m_hasSegmentsUrl = hasSegmentsUrl; }

  // Currently used for HLS only
  bool IsPrepared() const { return m_isPrepared; }
  // Currently used for HLS only
  void SetIsPrepared(bool isPrepared) { m_isPrepared = isPrepared; }

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

  CSegment* current_segment_{nullptr};


  bool HasInitSegment() const { return m_initSegment.has_value(); }
  void SetInitSegment(CSegment initSegment) { m_initSegment = initSegment; }
  std::optional<CSegment>& GetInitSegment() { return m_initSegment; }


  CSegment* get_next_segment(const CSegment* seg)
  {
    if (!seg || seg->IsInitialization())
      return m_segmentTimeline.Get(0);

    size_t nextPos{m_segmentTimeline.GetPosition(seg) + 1};
    if (nextPos == m_segmentTimeline.GetSize())
      return nullptr;

    return m_segmentTimeline.Get(nextPos);
  }

  CSegment* get_segment(size_t pos)
  {
    if (pos == SEGMENT_NO_POS)
      return nullptr;

    return m_segmentTimeline.Get(pos);
  }

  const size_t get_segment_pos(const CSegment* segment) const
  {
    if (!segment)
      return SEGMENT_NO_POS;

    return m_segmentTimeline.IsEmpty() ? 0 : m_segmentTimeline.GetPosition(segment);
  }

  const size_t getCurrentSegmentPos() const { return get_segment_pos(current_segment_); }

  const uint64_t getCurrentSegmentNumber() const
  {
    if (!current_segment_)
      return SEGMENT_NO_NUMBER;

    return static_cast<uint64_t>(get_segment_pos(current_segment_)) + m_startNumber;
  }

  const uint64_t getSegmentNumber(const CSegment* segment) const
  {
    if (!segment)
      return SEGMENT_NO_NUMBER;

    return static_cast<uint64_t>(get_segment_pos(segment)) + m_startNumber;
  }


  uint32_t timescale_ext_{0};
  uint32_t timescale_int_{0};

  void SetScaling()
  {
    if (!m_timescale)
    {
      timescale_ext_ = timescale_int_ = 1;
      return;
    }

    timescale_ext_ = 1000000;
    timescale_int_ = m_timescale;

    while (timescale_ext_ > 1)
    {
      if ((timescale_int_ / 10) * 10 == timescale_int_)
      {
        timescale_ext_ /= 10;
        timescale_int_ /= 10;
      }
      else
        break;
    }
  }

  std::chrono::time_point<std::chrono::system_clock> repLastUpdated_;

  //! @todo: appears to be stored for convenience, a refactor could remove it
  uint64_t nextPts_{0};

  //! @todo: to be reworked or deleted
  uint32_t assured_buffer_duration_{0};
  uint32_t max_buffer_duration_{0};

  //! @todo: used for HLS only, not reflect the right meaning
  bool m_isDownloaded{false};

protected:
  std::string m_id;
  std::string m_sourceUrl;
  std::string m_baseUrl;

  std::set<std::string> m_codecs;
  std::string m_codecPrivateData;

  uint32_t m_bandwidth{0}; // as bit/s

  uint16_t m_hdcpVersion{0}; // 0 if not set

  std::optional<CSegmentTemplate> m_segmentTemplate;
  std::optional<CSegmentBase> m_segmentBase;
  std::optional<CSegment> m_initSegment;

  uint64_t m_startNumber{1};

  CSpinCache<CSegment> m_segmentTimeline;

  uint64_t m_duration{0};
  uint32_t m_timescale{0};

  bool m_isSubtitleFileStream{false};
  bool m_hasInitPrefixed{false};

  bool m_hasSegmentsUrl{false};

  bool m_isPrepared{false};
  bool m_isEnabled{false};
  bool m_isWaitForSegment{false};

  bool m_isIncludedStream{false};
};

} // namespace PLAYLIST
