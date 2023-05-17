/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../common/AdaptiveTree.h"

namespace adaptive
{

class ATTR_DLL_LOCAL SmoothTree : public AdaptiveTree
{
public:
  SmoothTree(CHOOSER::IRepresentationChooser* reprChooser);
  SmoothTree(const SmoothTree& left);

  virtual void Configure(const UTILS::PROPERTIES::KodiProperties& kodiProps) override;

  virtual bool open(const std::string& url) override;
  virtual bool open(const std::string& url, std::map<std::string, std::string> addHeaders, bool isUpdate=false) override;

  virtual SmoothTree* Clone() const override { return new SmoothTree{*this}; }

  enum
  {
    SSMNODE_SSM = 1 << 0,
    SSMNODE_PROTECTION = 1 << 1,
    SSMNODE_STREAMINDEX = 1 << 2,
    SSMNODE_PROTECTIONHEADER = 1 << 3,
    SSMNODE_PROTECTIONTEXT = 1 << 4
  };

  uint64_t pts_helper_;

protected:
  virtual bool ParseManifest(const std::string& data);
};

} // namespace adaptive
