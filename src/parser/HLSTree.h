/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "Iaes_decrypter.h"
#include "common/AdaptiveTree.h"
#include "common/AdaptiveUtils.h"
#include "utils/CurlUtils.h"

namespace adaptive
{

class ATTR_DLL_LOCAL CHLSTree : public AdaptiveTree
{
public:
  enum class ParseStatus
  {
    SUCCESS,
    ERROR,
    INVALID, // Invalid manifest e.g. without segments
  };

  CHLSTree();
  virtual ~CHLSTree() {}

  virtual TreeType GetTreeType() const override { return TreeType::HLS; }

  CHLSTree(const CHLSTree& left);

  virtual CHLSTree* Clone() const override { return new CHLSTree{*this}; }

  virtual void Configure(CHOOSER::IRepresentationChooser* reprChooser,
                         std::vector<std::string_view> supportedKeySystems,
                         std::string_view manifestUpdateParam) override;

  virtual bool Open(std::string_view url,
                    const std::map<std::string, std::string>& headers,
                    const std::string& data) override;

  virtual bool PrepareRepresentation(PLAYLIST::CPeriod* period,
                                     PLAYLIST::CAdaptationSet* adp,
                                     PLAYLIST::CRepresentation* rep) override;

  virtual void OnDataArrived(uint64_t segNum,
                             uint16_t psshSet,
                             uint8_t iv[16],
                             const uint8_t* srcData,
                             size_t srcDataSize,
                             std::vector<uint8_t>& segBuffer,
                             size_t segBufferSize,
                             bool isLastChunk) override;

  virtual void OnStreamChange(PLAYLIST::CPeriod* period,
                              PLAYLIST::CAdaptationSet* adp,
                              PLAYLIST::CRepresentation* previousRep,
                              PLAYLIST::CRepresentation* currentRep) override;

  virtual void OnRequestSegments(PLAYLIST::CPeriod* period,
                                 PLAYLIST::CAdaptationSet* adp,
                                 PLAYLIST::CRepresentation* rep) override;

protected:
  // \brief Rendition features
  enum REND_FEATURE
  {
    REND_FEATURE_NONE,
    REND_FEATURE_EC3_JOC = 1 << 0
  };

  // \brief Usually refer to an EXT-X-MEDIA tag
  struct Rendition
  {
    std::string m_type;
    std::string m_groupId;
    std::string m_language;
    std::string m_name;
    bool m_isDefault{false};
    bool m_isForced{false};
    uint32_t m_channels{0};
    std::string m_characteristics;
    std::string m_uri;
    int m_features{REND_FEATURE_NONE};
    bool m_isUriDuplicate{false}; // Another rendition have same uri
  };

  // \brief Usually refer to an EXT-X-STREAM-INF tag
  struct Variant
  {
    uint32_t m_bandwidth{0};
    std::string m_codecs;
    std::string m_resolution;
    float m_frameRate{0};
    std::string m_groupIdAudio;
    std::string m_groupIdSubtitles;
    std::string m_uri;
    bool m_isUriDuplicate{false}; // Another variant have same uri
  };

  struct MultivariantPlaylist
  {
    std::vector<Rendition> m_audioRenditions;
    std::vector<Rendition> m_subtitleRenditions;
    std::vector<Variant> m_variants;
  };

  /*!
   * \brief Download the key from media initialization section, overridable method for test project
   */
  virtual bool DownloadKey(std::string_view url,
                           const std::map<std::string, std::string>& reqHeaders,
                           const std::vector<std::string>& respHeaders,
                           UTILS::CURL::HTTPResponse& resp);

  /*!
   * \brief Download manifest child, overridable method for test project
   */
  virtual bool DownloadManifestChild(std::string_view url,
                                     const std::map<std::string, std::string>& reqHeaders,
                                     const std::vector<std::string>& respHeaders,
                                     UTILS::CURL::HTTPResponse& resp);

  bool DownloadChildManifest(PLAYLIST::CAdaptationSet* adp,
                             PLAYLIST::CRepresentation* rep,
                             UTILS::CURL::HTTPResponse& resp);

  /*
   * \brief Check for inconsistent EXT-X-MEDIA-SEQUENCE value on manifest update,
   *        then correct the media sequence number.
   *        The corrected value is determined by finding the corresponding segment (PTS) in the updated playlist
   *        in order to work EXT-X-PROGRAM-DATE-TIME tag is needed.
   */
  void FixMediaSequence(std::stringstream& streamData,
                        uint64_t& mediaSeqNumber,
                        size_t adpSetPos,
                        size_t reprPos);

  /*
   * \brief Check for inconsistent EXT-X-DISCONTINUITY-SEQUENCE value on manifest update,
   *        then correct the discontinuity sequence number.
   *        The corrected value is determined by checking whether a segment falls within an existing period
   *        if found use that sequence number to fix EXT-X-DISCONTINUITY-SEQUENCE
   *        in order to work EXT-X-PROGRAM-DATE-TIME tag is needed.
   */
  void FixDiscSequence(std::stringstream& streamData, uint32_t& discSeqNumber);

  bool ProcessChildManifest(PLAYLIST::CPeriod* period,
                            PLAYLIST::CAdaptationSet* adp,
                            PLAYLIST::CRepresentation* rep,
                            uint64_t currentSegNumber);

  ParseStatus ParseChildManifest(const std::string& data,
                                 std::string_view sourceUrl,
                                 PLAYLIST::CPeriod* period,
                                 PLAYLIST::CAdaptationSet* adp,
                                 PLAYLIST::CRepresentation* rep);

  void PrepareSegments(PLAYLIST::CPeriod* period,
                       PLAYLIST::CAdaptationSet* adp,
                       PLAYLIST::CRepresentation* rep,
                       uint64_t segNumber);

  virtual void OnUpdateSegments() override;

  virtual bool ParseManifest(const std::string& stream);

  PLAYLIST::EncryptionType ProcessEncryption(std::string_view baseUrl,
                                             std::map<std::string, std::string>& attribs);

  bool GetUriByteData(std::string_view uri, std::vector<uint8_t>& data);

  /*!
   * \brief Parse a rendition and set the data to the AdaptationSet and Representation.
   * \param r The rendition
   * \param adpSet The adaptation set where set the data
   * \param repr The representation where set the data
   * \return True if success, otherwise false
   */
  bool ParseRenditon(const Rendition& r,
                     std::unique_ptr<PLAYLIST::CAdaptationSet>& adpSet,
                     std::unique_ptr<PLAYLIST::CRepresentation>& repr);

  /*!
   * \brief Parse a multivariant playlist
   * \param data The manifest data
   * \return True if success, otherwise false
   */
  bool ParseMultivariantPlaylist(const std::string& data);

  virtual void SaveManifest(PLAYLIST::CAdaptationSet* adpSet,
                            const std::string& data,
                            std::string_view info);


  std::unique_ptr<IAESDecrypter> m_decrypter;

private:
  /*!
   * \brief Find the first variant with the specified audio group id.
   * \param groupId The group id
   * \param variants The variants where search for
   * \return The variant if found, otherwise nullptr
   */
  const Variant* FindVariantByAudioGroupId(std::string groupId,
                                           std::vector<Variant>& variants) const;

  /*!
   * \brief Find the first rendition with the specified group id.
   * \param groupId The group id
   * \param renditions The renditions where search for
   * \return The rendition if found, otherwise nullptr
   */
  const Rendition* FindRenditionByGroupId(std::string groupId,
                                          std::vector<Rendition>& renditions) const;

  /*!
   * \brief Create and add a new adaptation set with an audio representaton,
   *        intended as included in the video, to the specified period.
   * \param period The period
   * \param codec The codec string of the audio stream
   */
  void AddIncludedAudioStream(std::unique_ptr<PLAYLIST::CPeriod>& period, std::string codec);

  /*!
   * \brief Find the period related to the specified discontinuity sequence number.
   * \param discNumber The sequence number
   * \return The period if found, otherwise nullptr
   */
  PLAYLIST::CPeriod* FindDiscontinuityPeriod(const uint32_t seqNumber);

  uint8_t m_segmentIntervalSec = 4;
  bool m_hasDiscontSeq = false;
  uint32_t m_discontSeq = 0;

  std::vector<uint8_t> m_currentPssh; // Last processed encryption PSSH from URI
  std::string m_currentDefaultKID; // Last processed encryption KID
  std::string m_currentKidUrl; // Last processed encryption KID URI
  std::string m_currentIV; // Last processed encryption IV
};

} // namespace
