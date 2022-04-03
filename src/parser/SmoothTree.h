/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../common/AdaptiveTree.h"

#include <kodi/AddonBase.h>

namespace adaptive
{

class ATTR_DLL_LOCAL SmoothTree : public AdaptiveTree
{
public:
  SmoothTree(const UTILS::PROPERTIES::KodiProperties& kodiProps,
             IRepresentationChooser* reprChooser);
  virtual bool open(const std::string& url, const std::string& manifestUpdateParam) override;
  virtual bool open(const std::string& url, const std::string& manifestUpdateParam, std::map<std::string, std::string> additionalHeaders) override;
  virtual bool write_data(void* buffer, size_t buffer_size, void* opaque) override;

  enum
  {
    SSMNODE_SSM = 1 << 0,
    SSMNODE_PROTECTION = 1 << 1,
    SSMNODE_STREAMINDEX = 1 << 2,
    SSMNODE_PROTECTIONHEADER = 1 << 3,
    SSMNODE_PROTECTIONTEXT = 1 << 4
  };

  uint64_t pts_helper_;
  };

}
