/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "common/AdaptiveTree.h"
#include "utils/CurlUtils.h"

#include <set>

namespace pugi
{
class xml_node;
}

namespace DRM
{
class PRHeaderParser;
}

namespace adaptive
{

class ATTR_DLL_LOCAL CSmoothTree : public AdaptiveTree
{
public:
  CSmoothTree();
  CSmoothTree(const CSmoothTree& left);

  virtual TreeType GetTreeType() const override { return TreeType::SMOOTH_STREAMING; }

  virtual bool Open(const std::string& url,
                    const std::map<std::string, std::string>& headers,
                    const std::string& data) override;

  virtual CSmoothTree* Clone() const override { return new CSmoothTree{*this}; }

  virtual void OnUpdateSegments() override;

protected:
  virtual bool ParseManifest(const std::string& data);

  void ParseTagStreamIndex(pugi::xml_node nodeSI,
                           PLAYLIST::CPeriod* period,
                           const std::vector<DRM::DRMInfo>& drmInfos,
                           std::set<uint64_t>& ptsStartList);
  void ParseTagQualityLevel(pugi::xml_node nodeQI,
                            PLAYLIST::CAdaptationSet* adpSet,
                            const uint32_t timescale,
                            const std::vector<DRM::DRMInfo>& drmInfos);
  void CreateSegmentTimeline();

  /*!
   * \brief Download manifest update, overridable method for test project
   */
  virtual bool DownloadManifestUpd(const std::string& url,
                                   const std::map<std::string, std::string>& reqHeaders,
                                   const std::vector<std::string>& respHeaders,
                                   UTILS::CURL::HTTPResponse& resp);

  void UpdateTotalTime();

  uint64_t m_ptsBase{0}; // Used on live stream to sync streams (when StreamIndex tags use async PTS for chunk start), in timescale
  uint64_t m_dvrWindowLength{0}; // DVRWindowLength attribute value, in ms
  uint64_t m_mediaPresDuration{0}; // Duration attribute value, in ms (may be not provided)
};

} // namespace adaptive
