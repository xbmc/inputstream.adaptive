/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AdaptiveUtils.h"
#include "CommonAttribs.h"
#include "CommonSegAttribs.h"
#include "Period.h"
#include "SegTemplate.h"
#include "SegmentList.h"

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace PLAYLIST
{
// Forward
class CRepresentation;

class ATTR_DLL_LOCAL CAdaptationSet : public CCommonSegAttribs, public CCommonAttribs
{
public:
  CAdaptationSet(CPeriod* parent = nullptr) : CCommonSegAttribs(parent), CCommonAttribs() {}
  ~CAdaptationSet() {}

  // Share Period common attribs
  static std::unique_ptr<CAdaptationSet> MakeUniquePtr(CPeriod* parent = nullptr)
  {
    return std::make_unique<CAdaptationSet>(parent);
  }

  std::string_view GetId() const { return m_id; }
  void SetId(std::string_view id) { m_id = id; }

  std::string GetName() const { return m_name; }
  void SetName(std::string_view name) { m_name = name; }

  std::string GetGroup() const { return m_group; }
  void SetGroup(std::string_view group) { m_group = group; }

  std::string GetBaseUrl() const { return m_baseUrl; }
  void SetBaseUrl(std::string_view baseUrl) { m_baseUrl = baseUrl; }

  uint64_t GetStartNumber() const { return m_startNumber; }
  void SetStartNumber(uint64_t startNumber) { m_startNumber = startNumber; }

  uint64_t GetStartPTS() const { return m_startPts; }
  void SetStartPTS(uint64_t startPts) { m_startPts = startPts; }

  void AddCodecs(std::string_view codecs);
  const std::set<std::string>& GetCodecs() { return m_codecs; }

  /*!
   * \brief Add codec strings
   */
  void AddCodecs(const std::set<std::string>& codecs);

  StreamType GetStreamType() const { return m_streamType; }
  void SetStreamType(StreamType streamType) { m_streamType = streamType; }

  // Check if a codec exists, convenient function to check within of strings
  // e.g find "ttml" return true also when there is a "stpp.ttml.im1t" codec name
  bool ContainsCodec(std::string_view codec);

  std::string GetLanguage() const { return m_language; }
  void SetLanguage(std::string_view language) { m_language = language; }

  void AddSwitchingIds(std::string_view switchingIds);
  const std::vector<std::string>& GetSwitchingIds() { return m_switchingIds; }

  CSpinCache<uint32_t>& SegmentTimelineDuration() { return m_segmentTimelineDuration; }
  bool HasSegmentTimelineDuration() { return !m_segmentTimelineDuration.IsEmpty(); }

  std::optional<CSegmentTemplate>& GetSegmentTemplate() { return m_segmentTemplate; }
  std::optional<CSegmentTemplate> GetSegmentTemplate() const { return m_segmentTemplate; }
  void SetSegmentTemplate(const CSegmentTemplate& segTemplate) { m_segmentTemplate = segTemplate; }
  bool HasSegmentTemplate() const { return m_segmentTemplate.has_value(); }

  void AddRepresentation(std::unique_ptr<CRepresentation>& representation);
  std::vector<std::unique_ptr<CRepresentation>>& GetRepresentations() { return m_representations; }
  std::vector<CRepresentation*> GetRepresentationsPtr();

  bool IsImpaired() const { return m_isImpaired; }
  void SetIsImpaired(bool isImpaired) { m_isImpaired = isImpaired; }

  bool IsOriginal() const { return m_isOriginal; }
  void SetIsOriginal(bool isOriginal) { m_isOriginal = isOriginal; }

  bool IsDefault() const { return m_isDefault; }
  void SetIsDefault(bool isDefault) { m_isDefault = isDefault; }

  bool IsForced() const { return m_isForced; }
  void SetIsForced(bool isForced) { m_isForced = isForced; }

  void CopyHLSData(const CAdaptationSet* other);

  bool IsMergeable(const CAdaptationSet* other) const;

  /*!
   * \brief Determine if an adaptation set is switchable with another one,
   *        as urn:mpeg:dash:adaptation-set-switching:2016 scheme
   * \param adpSets The adaptation set to compare
   * \return True if switchable, otherwise false
   */
  bool CompareSwitchingId(const CAdaptationSet* other) const;

  static bool Compare(const std::unique_ptr<CAdaptationSet>& left,
                      const std::unique_ptr<CAdaptationSet>& right);

  /*!
   * \brief Find an adaptation set by codec string.
   * \param adpSets The adaptation set list where to search
   * \param codec The codec string
   * \return The adaptation set if found, otherwise nullptr
   */
  static CAdaptationSet* FindByCodec(std::vector<std::unique_ptr<CAdaptationSet>>& adpSets,
                                     std::string codec);

  /*!
   * \brief Find a mergeable adaptation set by comparing properties.
   * \param adpSets The adaptation set list where to search
   * \param adpSet The adaptation set to be compared
   * \return The adaptation set if found, otherwise nullptr
   */
  static CAdaptationSet* FindMergeable(std::vector<std::unique_ptr<CAdaptationSet>>& adpSets,
                                       CAdaptationSet* adpSet);

protected:
  std::vector<std::unique_ptr<CRepresentation>> m_representations;

  std::string m_id;
  std::string m_name;
  std::string m_group;
  std::string m_baseUrl;
  uint64_t m_startNumber{1};
  uint64_t m_startPts{0};
  uint64_t m_duration{0};

  std::set<std::string> m_codecs;
  StreamType m_streamType{StreamType::NOTYPE};

  std::string m_language;
  std::vector<std::string> m_switchingIds;

  CSpinCache<uint32_t> m_segmentTimelineDuration;

  std::optional<CSegmentTemplate> m_segmentTemplate;

  // Custom ISAdaptive attributes (used on DASH only)
  bool m_isImpaired{false};
  bool m_isOriginal{false};
  bool m_isDefault{false};
  bool m_isForced{false};
};

} // namespace PLAYLIST
