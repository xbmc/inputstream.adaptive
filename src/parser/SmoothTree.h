/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../common/AdaptiveTree.h"

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
  CSmoothTree(CHOOSER::IRepresentationChooser* reprChooser);
  CSmoothTree(const CSmoothTree& left);

  virtual bool open(const std::string& url) override;
  virtual bool open(const std::string& url, std::map<std::string, std::string> addHeaders) override;

  virtual CSmoothTree* Clone() const override { return new CSmoothTree{*this}; }

protected:
  virtual bool ParseManifest(std::string& data);

  void ParseTagStreamIndex(pugi::xml_node nodeSI,
                           PLAYLIST::CPeriod* period,
                           const PRProtectionParser& protParser);
  void ParseTagQualityLevel(pugi::xml_node nodeQI,
                            PLAYLIST::CAdaptationSet* adpSet,
                            const uint32_t timescale,
                            const uint16_t psshSetPos);
};

} // namespace adaptive
