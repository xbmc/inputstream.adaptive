/*
*      Copyright (C) 2016-2016 peak3d
*      http://www.peak3d.de
*
*  This Program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This Program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  <http://www.gnu.org/licenses/>.
*
*/

#pragma once

#include "../common/AdaptiveTree.h"

namespace adaptive
{

  class DASHTree : public AdaptiveTree
  {
  public:
    DASHTree();
    virtual bool open(const std::string &url, const std::string &manifestUpdateParam) override;
    virtual bool write_data(void *buffer, size_t buffer_size, void *opaque) override;
    virtual void RefreshSegments(Representation *rep, StreamType type) override;

    void SetUpdateInterval(uint32_t interval) { updateInterval_ = interval; };
    uint64_t pts_helper_;
    uint32_t firstStartNumber_;
  protected:
    virtual void RefreshSegments() override;
  };
}
