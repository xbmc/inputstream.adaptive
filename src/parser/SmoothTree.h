/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "common/AdaptiveTree.h"

namespace pugi
{
class xml_node;
}

namespace adaptive
{
// Forward
class PRProtectionParser;

class ATTR_DLL_LOCAL CSmoothTree : public AdaptiveTree
{
public:
  CSmoothTree();
  CSmoothTree(const CSmoothTree& left);

  virtual TreeType GetTreeType() const override { return TreeType::SMOOTH_STREAMING; }

  virtual bool Open(std::string_view url,
                    const std::map<std::string, std::string>& headers,
                    const std::string& data) override;

  virtual CSmoothTree* Clone() const override { return new CSmoothTree{*this}; }

  virtual bool InsertLiveFragment(PLAYLIST::CAdaptationSet* adpSet,
                                  PLAYLIST::CRepresentation* repr,
                                  uint64_t fTimestamp,
                                  uint64_t fDuration,
                                  uint32_t fTimescale) override;

protected:
  virtual bool ParseManifest(const std::string& data);

  void ParseTagStreamIndex(pugi::xml_node nodeSI,
                           PLAYLIST::CPeriod* period,
                           const PRProtectionParser& protParser);
  void ParseTagQualityLevel(pugi::xml_node nodeQI,
                            PLAYLIST::CAdaptationSet* adpSet,
                            const uint32_t timescale,
                            const uint16_t psshSetPos);
  void CreateSegmentTimeline();

  uint64_t m_ptsBase{PLAYLIST::NO_PTS_VALUE}; // The lower start PTS time between all StreamIndex tags
};

} // namespace adaptive
