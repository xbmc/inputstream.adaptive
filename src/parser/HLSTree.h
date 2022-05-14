/*
 *  Copyright (C) 2017 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../common/AdaptiveTree.h"

#include <map>
#include <sstream>

#include <kodi/AddonBase.h>

class IAESDecrypter;

namespace adaptive
{

class ATTR_DLL_LOCAL HLSTree : public AdaptiveTree
{
public:
  enum
  {
    ENCRYPTIONTYPE_INVALID = 0,
    ENCRYPTIONTYPE_CLEAR = 1,
    ENCRYPTIONTYPE_AES128 = 2,
    ENCRYPTIONTYPE_WIDEVINE = 3,
    ENCRYPTIONTYPE_UNKNOWN = 4,
  };
  HLSTree(const UTILS::PROPERTIES::KodiProperties& kodiProps, IAESDecrypter* decrypter)
    : AdaptiveTree(kodiProps), m_decrypter(decrypter){};
  virtual ~HLSTree();

  virtual bool open(const std::string& url, const std::string& manifestUpdateParam) override;
  virtual bool open(const std::string& url, const std::string& manifestUpdateParam, std::map<std::string, std::string> additionalHeaders) override;
  virtual PREPARE_RESULT prepareRepresentation(Period* period,
                                               AdaptationSet* adp,
                                               Representation* rep,
                                               bool update = false) override;
  virtual bool write_data(void* buffer, size_t buffer_size, void* opaque) override;
  virtual void OnDataArrived(uint64_t segNum,
                             uint16_t psshSet,
                             uint8_t iv[16],
                             const uint8_t* src,
                             uint8_t* dst,
                             size_t dstOffset,
                             size_t dataSize) override;
  virtual void RefreshSegments(Period* period,
                               AdaptationSet* adp,
                               Representation* rep,
                               StreamType type) override;
  virtual bool processManifest(std::stringstream& stream);
  virtual std::chrono::time_point<std::chrono::system_clock> GetRepLastUpdated(const Representation* rep)
  { 
    return rep->repLastUpdated_;
  }
  
protected:
  virtual void RefreshLiveSegments() override;

private:
  int processEncryption(std::string baseUrl, std::map<std::string, std::string>& map);
  std::string m_audioCodec;

  struct EXTGROUP
  {
    std::string m_codec;
    std::vector<AdaptationSet*> m_sets;

    void setCodec(const std::string& codec)
    {
      if (m_codec.empty())
      {
        m_codec = codec;
        for (auto& set : m_sets)
          set->representations_[0]->codecs_ = codec;
      }
    };
  };

  std::map<std::string, EXTGROUP> m_extGroups;
  bool m_refreshPlayList = true;
  uint8_t m_segmentIntervalSec = 4;
  IAESDecrypter *m_decrypter;
  std::stringstream manifest_stream;
  bool m_hasDiscontSeq = false;
  uint32_t m_discontSeq = 0;
};

} // namespace
