/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "common/AdaptiveTree.h"
#include "common/AdaptiveUtils.h"
#include "common/SegTemplate.h"
#include "utils/CurlUtils.h"

#include <string_view>

// Forward
namespace pugi
{
class xml_node;
}
namespace PLAYLIST
{
struct ProtectionScheme;
}

namespace adaptive
{

class ATTR_DLL_LOCAL CDashTree : public adaptive::AdaptiveTree
{
public:
  CDashTree() : AdaptiveTree() {}
  CDashTree(const CDashTree& left);

  void Configure(CHOOSER::IRepresentationChooser* reprChooser,
                 std::vector<std::string_view> supportedKeySystems,
                 std::string_view manifestUpdParams) override;

  virtual TreeType GetTreeType() const override { return TreeType::DASH; }

  virtual bool Open(std::string_view url,
                    const std::map<std::string, std::string>& headers,
                    const std::string& data) override;

  virtual bool InsertLiveSegment(PLAYLIST::CPeriod* period,
                                 PLAYLIST::CAdaptationSet* adpSet,
                                 PLAYLIST::CRepresentation* repr,
                                 size_t pos) override;

  virtual bool InsertLiveFragment(PLAYLIST::CAdaptationSet* adpSet,
                                  PLAYLIST::CRepresentation* repr,
                                  uint64_t fTimestamp,
                                  uint64_t fDuration,
                                  uint32_t fTimescale) override;

protected:
  virtual CDashTree* Clone() const override { return new CDashTree{*this}; }

  virtual bool ParseManifest(const std::string& data);

  void ParseTagMPDAttribs(pugi::xml_node NodeMPD);
  void ParseTagPeriod(pugi::xml_node nodePeriod, std::string_view mpdUrl);
  void ParseTagAdaptationSet(pugi::xml_node nodeAdp, PLAYLIST::CPeriod* period);
  void ParseTagRepresentation(pugi::xml_node nodeRepr,
                              PLAYLIST::CAdaptationSet* adpSet,
                              PLAYLIST::CPeriod* period);

  void ParseTagSegmentTimeline(pugi::xml_node parentNode,
                               std::vector<uint32_t>& SCTimeline);

  void ParseSegmentTemplate(pugi::xml_node node, PLAYLIST::CSegmentTemplate& segTpl);

  void ParseTagContentProtection(pugi::xml_node nodeParent,
                                 std::vector<PLAYLIST::ProtectionScheme>& protectionSchemes);

  /*!
   * \brief Get the protection data for the representation
   * \param adpProtSchemes The protection schemes of the adaptation set relative to the representation
   * \param reprProtSchemes The protection schemes of the representation
   * \param pssh[OUT] The PSSH (if any) that match the supported systemid
   * \param kid[OUT] The KID (should be provided)
   * \param licenseUrl[OUT] The license url (if any)
   * \return True if a protection has been found, otherwise false
   */
  bool GetProtectionData(const std::vector<PLAYLIST::ProtectionScheme>& adpProtSchemes,
                         const std::vector<PLAYLIST::ProtectionScheme>& reprProtSchemes,
                         std::vector<uint8_t>& pssh,
                         std::string& kid,
                         std::string& licenseUrl);

  bool ParseTagContentProtectionSecDec(pugi::xml_node nodeParent);

  uint32_t ParseAudioChannelConfig(pugi::xml_node node);

  void MergeAdpSets();

  /*!
   * \brief Download manifest update, overridable method for test project
   */
  virtual bool DownloadManifestUpd(std::string_view url,
                                   const std::map<std::string, std::string>& reqHeaders,
                                   const std::vector<std::string>& respHeaders,
                                   UTILS::CURL::HTTPResponse& resp);

  virtual void OnRequestSegments(PLAYLIST::CPeriod* period,
                                 PLAYLIST::CAdaptationSet* adp,
                                 PLAYLIST::CRepresentation* rep) override;

  virtual void OnUpdateSegments() override;

  // The lower start number of segments
  uint64_t m_segmentsLowerStartNumber{0};

  std::map<std::string, std::string> m_manifestRespHeaders;

  // Period sequence incremented to every new period added
  uint32_t m_periodCurrentSeq{0};

  uint64_t m_timeShiftBufferDepth{0}; // MPD Timeshift buffer attribute value, in ms
  uint64_t m_mediaPresDuration{0}; // MPD Media presentation duration attribute value, in ms (may be not provided)

  uint64_t m_minimumUpdatePeriod{PLAYLIST::NO_VALUE}; // in seconds, NO_VALUE if not set

  // Determines if a custom PSSH initialization license data is provided
  bool m_isCustomInitPssh{false};
};
} // namespace adaptive
