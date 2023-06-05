/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../Iaes_decrypter.h"
#include "../common/AdaptiveTree.h"
#include "../common/AdaptiveUtils.h"
#include "../utils/CurlUtils.h"

#include <map>

namespace adaptive
{

class ATTR_DLL_LOCAL CHLSTree : public AdaptiveTree
{
public:
  CHLSTree() : AdaptiveTree() {}
  virtual ~CHLSTree() {}

  CHLSTree(const CHLSTree& left);

  virtual CHLSTree* Clone() const override { return new CHLSTree{*this}; }

  virtual void Configure(const UTILS::PROPERTIES::KodiProperties& kodiProps,
                         CHOOSER::IRepresentationChooser* reprChooser,
                         std::string_view supportedKeySystem,
                         std::string_view manifestUpdateParam) override;

  virtual bool Open(std::string_view url,
                    const std::map<std::string, std::string>& headers,
                    const std::string& data) override;

  virtual PLAYLIST::PrepareRepStatus prepareRepresentation(PLAYLIST::CPeriod* period,
                                                            PLAYLIST::CAdaptationSet* adp,
                                                            PLAYLIST::CRepresentation* rep,
                                                            bool update = false) override;

  virtual void OnDataArrived(uint64_t segNum,
                             uint16_t psshSet,
                             uint8_t iv[16],
                             const char* srcData,
                             size_t srcDataSize,
                             std::string& segBuffer,
                             size_t segBufferSize,
                             bool isLastChunk) override;

  virtual void RefreshSegments(PLAYLIST::CPeriod* period,
                               PLAYLIST::CAdaptationSet* adp,
                               PLAYLIST::CRepresentation* rep,
                               PLAYLIST::StreamType type) override;

protected:
  /*!
   * \brief Download the key from media initialization section, overridable method for test project
   */
  virtual bool DownloadKey(std::string_view url,
                           const std::map<std::string, std::string>& reqHeaders,
                           const std::vector<std::string>& respHeaders,
                           UTILS::CURL::HTTPResponse& resp);

  /*!
   * \brief Download manifest child, overridable method for test project
   */
  virtual bool DownloadManifestChild(std::string_view url,
                                     const std::map<std::string, std::string>& reqHeaders,
                                     const std::vector<std::string>& respHeaders,
                                     UTILS::CURL::HTTPResponse& resp);

  virtual void RefreshLiveSegments() override;

  virtual bool ParseManifest(const std::string& stream);

  PLAYLIST::EncryptionType ProcessEncryption(std::string_view baseUrl,
                                             std::map<std::string, std::string>& attribs);

  virtual void SaveManifest(PLAYLIST::CAdaptationSet* adpSet,
                            const std::string& data,
                            std::string_view info);

  std::unique_ptr<IAESDecrypter> m_decrypter;

private:
  struct ExtGroup
  {
    std::string m_codecs;
    std::vector<std::unique_ptr<PLAYLIST::CAdaptationSet>> m_adpSets;

    // Apply codecs to the first representation of each adaptation set
    void SetCodecs(std::string_view codecs)
    {
      if (m_codecs.empty())
      {
        m_codecs = codecs;
        for (auto& adpSet : m_adpSets)
        {
          adpSet->GetRepresentations()[0]->AddCodecs(codecs);
        }
      }
    };
  };

  std::map<std::string, ExtGroup> m_extGroups;
  bool m_refreshPlayList = true;
  uint8_t m_segmentIntervalSec = 4;
  bool m_hasDiscontSeq = false;
  uint32_t m_discontSeq = 0;

  std::string m_currentPssh; // Last processed encryption URI
  std::string m_currentDefaultKID; // Last processed encryption KID
  std::string m_currentIV; // Last processed encryption IV
};

} // namespace
