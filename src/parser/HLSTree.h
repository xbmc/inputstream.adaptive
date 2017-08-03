/*
*      Copyright (C) 2017 peak3d
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
#include <sstream>
#include <map>

namespace adaptive
{

  class HLSTree : public AdaptiveTree
  {
  public:
    HLSTree() = default;
    virtual ~HLSTree() = default;

    virtual bool open(const char *url) override;
    virtual bool prepareRepresentation(Representation *rep, uint64_t segmentId = 0) override;
    virtual bool write_data(void *buffer, size_t buffer_size) override;
    virtual void OnSegmentDownloaded(Representation *rep, const Segment *seg, uint8_t *data, size_t dataSize) override;
  private:
    std::stringstream m_stream;
    std::string m_audioCodec;

    struct EXTGROUP
    {
      std::string m_codec;
      std::vector<AdaptationSet*> m_sets;

      void setCodec(const std::string &codec)
      {
        if (m_codec.empty())
        {
          m_codec = codec;
          for (auto &set : m_sets)
            set->repesentations_[0]->codecs_ = codec;
        }
      }
    };

    std::map<std::string, EXTGROUP> m_extGroups;
  };

} // namespace
