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

class ATTR_DLL_LOCAL DASHTree : public AdaptiveTree
{
public:
  DASHTree(CHOOSER::IRepresentationChooser* reprChooser) : AdaptiveTree(reprChooser) {}
  DASHTree(const DASHTree& left);

  virtual bool open(const std::string& url) override;
  virtual bool open(const std::string& url, std::map<std::string, std::string> addHeaders) override;
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
  void SetUpdateInterval(uint32_t interval) { m_updateInterval = interval; };

  /*!
   * \brief Set the manifest update url parameter, used to force enabling manifest updates.
   *        This implementation has optional support to use the placeholder $START_NUMBER$
   *        to make next manifest update requests by adding a parameter with segment start number.
   *        e.g. ?start_seq=$START_NUMBER$
   *        This can be set directly in the manifest url or as separate parameter (see Kodi property).
   * \param manifestUrl The manifest url, if contains the placeholder $START_NUMBER$ the
   *                    the parameter will be removed from the original url value,
   *                    and used to request next manifest updates.
   * \param param The update parameter, is accepted "full" value,
   *              or an url parameter with $START_NUMBER$ placeholder.
   */
  virtual void SetManifestUpdateParam(std::string& manifestUrl, std::string_view param) override;

  uint64_t pts_helper_, timeline_time_;
  uint64_t firstStartNumber_;
  uint32_t current_sequence_;
  std::string current_playready_wrmheader_;
  std::string mpd_url_;

protected:
  virtual bool ParseManifest(const std::string& data);
  virtual void RefreshLiveSegments() override;

  virtual DASHTree* Clone() const override { return new DASHTree{*this}; }

  HTTPRespHeaders m_manifestRespHeaders;
};
} // namespace adaptive
