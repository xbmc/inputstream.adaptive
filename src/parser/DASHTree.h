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
    virtual bool open(const char *url) override;
    virtual bool write_data(void *buffer, size_t buffer_size) override;

    enum
    {
      MPDNODE_MPD = 1 << 0,
      MPDNODE_PERIOD = 1 << 1,
      MPDNODE_ADAPTIONSET = 1 << 2,
      MPDNODE_CONTENTPROTECTION = 1 << 3,
      MPDNODE_REPRESENTATION = 1 << 4,
      MPDNODE_BASEURL = 1 << 5,
      MPDNODE_SEGMENTLIST = 1 << 6,
      MPDNODE_INITIALIZATION = 1 << 7,
      MPDNODE_SEGMENTURL = 1 << 8,
      MPDNODE_SEGMENTDURATIONS = 1 << 9,
      MPDNODE_S = 1 << 11,
      MPDNODE_PSSH = 1 << 12,
      MPDNODE_SEGMENTTEMPLATE = 1 << 13,
      MPDNODE_SEGMENTTIMELINE = 1 << 14
    };
  };

}
