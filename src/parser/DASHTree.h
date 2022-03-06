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

class ATTR_DLL_LOCAL DASHTree : public AdaptiveTree
{
public:
  DASHTree(const UTILS::PROPERTIES::KodiProperties& kodiProps) : AdaptiveTree(kodiProps){};
  virtual bool open(const std::string& url, const std::string& manifestUpdateParam) override;
  virtual bool open(const std::string& url, const std::string& manifestUpdateParam, std::map<std::string, std::string> additionalHeaders) override;
  virtual bool write_data(void* buffer, size_t buffer_size, void* opaque) override;
  virtual void RefreshSegments(Period* period,
                               AdaptationSet* adp,
                               Representation* rep,
                               StreamType type) override;

  virtual uint64_t GetNowTime() { return time(0); };
  virtual std::chrono::system_clock::time_point GetTimePointNowTime()
  {
    return std::chrono::system_clock::now();
  };
  virtual void SetLastUpdated(std::chrono::system_clock::time_point tm){};
  void SetUpdateInterval(uint32_t interval) { updateInterval_ = interval; };
  uint64_t pts_helper_, timeline_time_;
  uint64_t firstStartNumber_;
  std::string current_playready_wrmheader_;
  std::string mpd_url_;

protected:
  virtual void RefreshLiveSegments() override;
  };
}
