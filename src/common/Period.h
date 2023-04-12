/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AdaptiveUtils.h"
#include "CommonSegAttribs.h"
#include "SegTemplate.h"
#include "SegmentList.h"
#include "../utils/CryptoUtils.h"

#ifdef INPUTSTREAM_TEST_BUILD
#include "../test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace PLAYLIST
{
// Forward
class CAdaptationSet;

class ATTR_DLL_LOCAL CPeriod : public CCommonSegAttribs
{
public:
  CPeriod();
  ~CPeriod();

  static std::unique_ptr<CPeriod> MakeUniquePtr() { return std::make_unique<CPeriod>(); }

  std::string_view GetId() const { return m_id; }
  void SetId(std::string_view id) { m_id = id; }

  std::string GetBaseUrl() const { return m_baseUrl; }
  void SetBaseUrl(std::string_view baseUrl) { m_baseUrl = baseUrl; }

  //! @todo: SetTimescale can be set/updated by period, adaptationSet and/or representation
  //! maybe is possible improve how are updated these variables in a better way
  uint32_t GetTimescale() const { return m_timescale; }
  void SetTimescale(uint32_t timescale) { m_timescale = timescale; }

  uint32_t GetSequence() const { return m_sequence; }
  void SetSequence(uint32_t sequence) { m_sequence = sequence; }

  uint64_t GetStart() const { return m_start; }
  void SetStart(uint64_t start) { m_start = start; }

  uint64_t GetStartPTS() const { return m_startPts; }
  void SetStartPTS(uint64_t startPts) { m_startPts = startPts; }
  
  // Could be set also by adaptation set or representation (in ms)
  uint64_t GetDuration() const { return m_duration; }
  void SetDuration(uint64_t duration) { m_duration = duration; }
  
  EncryptionState GetEncryptionState() const { return m_encryptionState; }
  void SetEncryptionState(EncryptionState encryptState) { m_encryptionState = encryptState; }

  // Force the use of secure decoder only when parsed manifest specify it
  uint64_t IsSecureDecodeNeeded() const { return m_isSecureDecoderNeeded; }
  void SetSecureDecodeNeeded(uint64_t isSecureDecoderNeeded)
  {
    m_isSecureDecoderNeeded = isSecureDecoderNeeded;
  };

  CSpinCache<uint32_t>& SegmentTimelineDuration() { return m_segmentTimelineDuration; }
  bool HasSegmentTimelineDuration() { return !m_segmentTimelineDuration.IsEmpty(); }

  std::optional<CSegmentTemplate>& GetSegmentTemplate() { return m_segmentTemplate; }
  std::optional<CSegmentTemplate> GetSegmentTemplate() const { return m_segmentTemplate; }
  void SetSegmentTemplate(const CSegmentTemplate& segTemplate) { m_segmentTemplate = segTemplate; }
  bool HasSegmentTemplate() const { return m_segmentTemplate.has_value(); }

  void CopyHLSData(const CPeriod* other);

  void AddAdaptationSet(std::unique_ptr<CAdaptationSet>& adaptationSet);
  std::vector<std::unique_ptr<CAdaptationSet>>& GetAdaptationSets() { return m_adaptationSets; }

  struct ATTR_DLL_LOCAL PSSHSet
  {
    static constexpr uint32_t MEDIA_UNSPECIFIED = 0;
    static constexpr uint32_t MEDIA_VIDEO = 1;
    static constexpr uint32_t MEDIA_AUDIO = 2;

    // Custom comparator for std::find
    bool operator==(const PSSHSet& other) const
    {
      return m_usageCount == 0 || (media_ == other.media_ && pssh_ == other.pssh_ &&
                                   defaultKID_ == other.defaultKID_ && iv == other.iv);
    }

    //! @todo: create getter/setters
    std::string pssh_;
    std::string defaultKID_;
    std::string iv;
    uint32_t media_{0};
    // Specify how many times the same PSSH is used between AdaptationSets or Representations
    uint32_t m_usageCount{0};
    CryptoMode m_cryptoMode{CryptoMode::NONE};
    CAdaptationSet* adaptation_set_{nullptr};
  };

  uint16_t InsertPSSHSet(PSSHSet* pssh);
  void InsertPSSHSet(uint16_t pssh_set) { m_psshSets[pssh_set].m_usageCount++; }
  void RemovePSSHSet(uint16_t pssh_set);
  void DecrasePSSHSetUsageCount(uint16_t pssh_set) { m_psshSets[pssh_set].m_usageCount--; }
  std::vector<PSSHSet>& GetPSSHSets() { return m_psshSets; }

  // Make use of PLAYLIST::StreamType flags
  uint32_t m_includedStreamType{0}; //! @todo: part of this need a rework

protected:
  std::vector<std::unique_ptr<CAdaptationSet>> m_adaptationSets;
  
  std::vector<PSSHSet> m_psshSets;

  std::string m_id;
  std::string m_baseUrl;
  uint32_t m_timescale{1000};
  uint32_t m_sequence{0};
  uint64_t m_start{0};
  uint64_t m_startPts{0};
  uint64_t m_duration{0};
  EncryptionState m_encryptionState{EncryptionState::UNENCRYPTED};
  bool m_isSecureDecoderNeeded{false};
  CSpinCache<uint32_t> m_segmentTimelineDuration;
  std::optional<CSegmentTemplate> m_segmentTemplate;
};

} // namespace adaptive
