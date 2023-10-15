/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "CdmBuffer.h"

#include "cdm/media/cdm/cdm_adapter.h"
#include <bento4/Ap4.h>
#include <kodi/addon-instance/VideoCodec.h>

class CWVDecrypter;
class CWVCencSingleSampleDecrypter;

class ATTR_DLL_LOCAL CWVCdmAdapter : public media::CdmAdapterClient
{
public:
  CWVCdmAdapter(std::string_view licenseURL,
                const std::vector<uint8_t>& serverCert,
                const uint8_t config,
                CWVDecrypter* host);
  virtual ~CWVCdmAdapter();

  virtual void OnCDMMessage(const char* session,
                            uint32_t session_size,
                            CDMADPMSG msg,
                            const uint8_t* data,
                            size_t data_size,
                            uint32_t status) override;

  virtual cdm::Buffer* AllocateBuffer(size_t sz) override;

  void insertssd(CWVCencSingleSampleDecrypter* ssd) { ssds.push_back(ssd); };
  void removessd(CWVCencSingleSampleDecrypter* ssd)
  {
    std::vector<CWVCencSingleSampleDecrypter*>::iterator res(
        std::find(ssds.begin(), ssds.end(), ssd));
    if (res != ssds.end())
      ssds.erase(res);
  };

  media::CdmAdapter* GetCdmAdapter() { return wv_adapter.get(); };
  const std::string& GetLicenseURL() { return m_licenseUrl; };

  cdm::Status DecryptAndDecodeFrame(cdm::InputBuffer_2& cdm_in,
                                    media::CdmVideoFrame* frame,
                                    kodi::addon::CInstanceVideoCodec* codecInstance)
  {
    // DecryptAndDecodeFrame calls CdmAdapter::Allocate which calls Host->GetBuffer
    // that cast hostInstance to CInstanceVideoCodec to get the frame buffer
    // so we have temporary set the host instance
    m_codecInstance = codecInstance;
    cdm::Status ret = wv_adapter->DecryptAndDecodeFrame(cdm_in, frame);
    m_codecInstance = nullptr;
    return ret;
  }

private:
  std::shared_ptr<media::CdmAdapter> wv_adapter;
  std::string m_licenseUrl;
  kodi::addon::CInstanceVideoCodec* m_codecInstance;
  CWVDecrypter* m_host;
  std::vector<CWVCencSingleSampleDecrypter*> ssds;
};
