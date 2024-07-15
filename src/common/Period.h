/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "CommonSegAttribs.h"
#include "SegTemplate.h"
#include "utils/CryptoUtils.h"

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
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

  uint32_t GetSequence() const { return m_sequence; }
  void SetSequence(uint32_t sequence) { m_sequence = sequence; }

  /*!
   * \brief Get the start time, in ms.
   * \return The start time value, otherwise NO_VALUE if not set.
   */
  uint64_t GetStart() const { return m_start; }

  /*!
   * \brief Set the start time in ms, or NO_VALUE for not set.
   */
  void SetStart(uint64_t start) { m_start = start; }

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
   * \return The timescale unit, if not set default value is 1000.
   */
  uint32_t GetTimescale() const { return m_timescale; }

  /*!
   * \brief Set the timescale unit.
   */
  void SetTimescale(uint32_t timescale) { m_timescale = timescale; }

  EncryptionState GetEncryptionState() const { return m_encryptionState; }
  void SetEncryptionState(EncryptionState encryptState) { m_encryptionState = encryptState; }

  // Force the use of secure decoder only when parsed manifest specify it
  uint64_t IsSecureDecodeNeeded() const { return m_isSecureDecoderNeeded; }
  void SetSecureDecodeNeeded(uint64_t isSecureDecoderNeeded)
  {
    m_isSecureDecoderNeeded = isSecureDecoderNeeded;
  };

  std::vector<uint32_t>& SegmentTimelineDuration() { return m_segmentTimelineDuration; }
  bool HasSegmentTimelineDuration() { return !m_segmentTimelineDuration.empty(); }

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
      return media_ == other.media_ && pssh_ == other.pssh_ && defaultKID_ == other.defaultKID_ &&
             iv == other.iv;
    }

    //! @todo: create getter/setters
    std::vector<uint8_t> pssh_; // Data as bytes (not base64)
    std::string m_licenseUrl; // License server URL
    std::string defaultKID_;
    std::string iv;
    uint32_t media_{0};
    // Specify how many times the same PSSH is used between AdaptationSets or Representations
    uint32_t m_usageCount{0};
    CryptoMode m_cryptoMode{CryptoMode::NONE};
    CAdaptationSet* adaptation_set_{nullptr};
  };

  uint16_t InsertPSSHSet(const PSSHSet& pssh);
  void RemovePSSHSet(uint16_t pssh_set);
  void DecreasePSSHSetUsageCount(uint16_t pssh_set);
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
  uint64_t m_start{NO_VALUE};
  uint64_t m_duration{0};
  EncryptionState m_encryptionState{EncryptionState::UNENCRYPTED};
  bool m_isSecureDecoderNeeded{false};
  std::vector<uint32_t> m_segmentTimelineDuration;
};

} // namespace adaptive
