/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "common/AdaptationSet.h"
#include "common/AdaptiveTree.h"
#include "common/AdaptiveUtils.h"
#include "common/Period.h"
#include "common/SegTemplate.h"
#include "utils/CurlUtils.h"

#include <string_view>

// Forward
namespace pugi
{
class xml_node;
}

namespace adaptive
{

class ATTR_DLL_LOCAL CDashTree : public adaptive::AdaptiveTree
{
public:
  CDashTree() : AdaptiveTree() {}
  CDashTree(const CDashTree& left);

  void Configure(const UTILS::PROPERTIES::KodiProperties& kodiProps,
                 CHOOSER::IRepresentationChooser* reprChooser,
                 std::string_view supportedKeySystem,
                 std::string_view manifestUpdParams) override;

  virtual TreeType GetTreeType() override { return TreeType::DASH; }

  virtual bool Open(std::string_view url,
                    const std::map<std::string, std::string>& headers,
                    const std::string& data) override;

  virtual void InsertLiveSegment(PLAYLIST::CPeriod* period,
                                 PLAYLIST::CAdaptationSet* adpSet,
                                 PLAYLIST::CRepresentation* repr,
                                 size_t pos,
                                 uint64_t timestamp,
                                 uint64_t fragmentDuration,
                                 uint32_t movieTimescale) override;

protected:
  virtual CDashTree* Clone() const override { return new CDashTree{*this}; }

  virtual bool ParseManifest(const std::string& data);

  void ParseTagMPDAttribs(pugi::xml_node NodeMPD);
  void ParseTagPeriod(pugi::xml_node nodePeriod, std::string_view mpdUrl);
  void ParseTagAdaptationSet(pugi::xml_node nodeAdp, PLAYLIST::CPeriod* period);
  void ParseTagRepresentation(pugi::xml_node nodeRepr,
                              PLAYLIST::CAdaptationSet* adpSet,
                              PLAYLIST::CPeriod* period);

  uint64_t ParseTagSegmentTimeline(pugi::xml_node parentNode,
                                   PLAYLIST::CSpinCache<uint32_t>& SCTimeline,
                                   uint32_t timescale = 1000);
  uint64_t ParseTagSegmentTimeline(pugi::xml_node nodeSegTL,
                                   PLAYLIST::CSpinCache<PLAYLIST::CSegment>& SCTimeline,
                                   uint32_t timescale = 1000,
                                   uint64_t totalTimeSecs = 0,
                                   PLAYLIST::CSegmentTemplate* segTemplate = nullptr);

  void ParseSegmentTemplate(pugi::xml_node node, PLAYLIST::CSegmentTemplate* segTpl);

  bool ParseTagContentProtection(pugi::xml_node nodeCP, std::string& pssh, std::string& kid);

  bool ParseTagContentProtectionSecDec(pugi::xml_node nodeParent);

  uint32_t ParseAudioChannelConfig(pugi::xml_node node);

  /*
   * \brief Estimate the count of segments on the period duration
   */
  size_t EstimateSegmentsCount(uint64_t duration, uint32_t timescale, uint64_t totalTimeSecs = 0);

  void MergeAdpSets();

  /*!
   * \brief Download manifest update, overridable method for test project
   */
  virtual bool DownloadManifestUpd(std::string_view url,
                                   const std::map<std::string, std::string>& reqHeaders,
                                   const std::vector<std::string>& respHeaders,
                                   UTILS::CURL::HTTPResponse& resp);

  virtual void RefreshSegments(PLAYLIST::CPeriod* period,
                               PLAYLIST::CAdaptationSet* adp,
                               PLAYLIST::CRepresentation* rep,
                               PLAYLIST::StreamType type) override;

  virtual void RefreshLiveSegments() override;

  /*
   * \brief Get the current timestamp, overridable method for test project
   */
  virtual uint64_t GetTimestamp();

  uint64_t m_firstStartNumber{0};

  // The lower start number of segments
  uint64_t m_segmentsLowerStartNumber{0};

  std::map<std::string, std::string> m_manifestRespHeaders;

  // Period sequence incremented to every new period added
  uint32_t m_periodCurrentSeq{0};

  uint64_t m_minimumUpdatePeriod{0}; // in seconds
  bool m_allowInsertLiveSegments{false};
  // Determines if a custom PSSH initialization license data is provided
  bool m_isCustomInitPssh{false};

  struct ProtectionScheme
  {
    std::string idUri;
    std::string value;
    std::string kid;
    std::string pssh;
  };
};
} // namespace adaptive
