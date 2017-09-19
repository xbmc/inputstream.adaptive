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

class AESDecrypter;

namespace adaptive
{

  class HLSTree : public AdaptiveTree
  {
  public:
    HLSTree(AESDecrypter *decrypter) : AdaptiveTree(), m_decrypter(decrypter) {};
    virtual ~HLSTree();

    virtual bool open(const std::string &url, const std::string &manifestUpdateParam) override;
    virtual bool prepareRepresentation(Representation *rep, bool update = false) override;
    virtual bool write_data(void *buffer, size_t buffer_size, void *opaque) override;
    virtual void OnDataArrived(unsigned int segNum, uint16_t psshSet, uint8_t iv[16], const uint8_t *src, uint8_t *dst, size_t dstOffset, size_t dataSize) override;
    virtual void RefreshSegments(Representation *rep, StreamType type) override;

  protected:
    virtual void RefreshSegments() override;

  private:
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
    bool m_refreshPlayList = true;
    uint8_t m_segmentIntervalSec = 4;
    AESDecrypter *m_decrypter;
  };

} // namespace
