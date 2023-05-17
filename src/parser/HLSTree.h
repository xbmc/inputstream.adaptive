/*
 *  Copyright (C) 2017 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../common/AdaptiveTree.h"
#include "../Iaes_decrypter.h"

#include <map>
#include <sstream>

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
  HLSTree(CHOOSER::IRepresentationChooser* reprChooser);
  HLSTree(const HLSTree& left);

  virtual void Configure(const UTILS::PROPERTIES::KodiProperties& kodiProps) override;

  virtual ~HLSTree();

  virtual bool open(const std::string& url) override;
  virtual bool open(const std::string& url, std::map<std::string, std::string> additionalHeaders, bool isUpdate=false) override;
  virtual PREPARE_RESULT prepareRepresentation(Period* period,
                                               AdaptationSet* adp,
                                               Representation* rep,
                                               bool update = false) override;

  virtual void OnDataArrived(uint64_t segNum,
                             uint16_t psshSet,
                             uint8_t iv[16],
                             const uint8_t* src,
                             std::string& dst,
                             size_t dstOffset,
                             size_t dataSize,
                             bool lastChunk) override;
  virtual void RefreshSegments(Period* period,
                               AdaptationSet* adp,
                               Representation* rep,
                               StreamType type) override;

  virtual std::chrono::time_point<std::chrono::system_clock> GetRepLastUpdated(
      const Representation* rep) override
  {
    return rep->repLastUpdated_;
  }
  
  virtual HLSTree* Clone() const override { return new HLSTree{*this}; }

protected:
  virtual bool ParseManifest(std::stringstream& stream);
  virtual void RefreshLiveSegments() override;

  virtual void SaveManifest(AdaptationSet* adp,
                            const std::stringstream& data,
                            std::string_view info);

  std::unique_ptr<IAESDecrypter> m_decrypter;

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
  bool m_hasDiscontSeq = false;
  uint32_t m_discontSeq = 0;
};

} // namespace
