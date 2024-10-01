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
#include "decrypters/HelperWv.h"
#include "decrypters/IDecrypter.h"

#include <list>
#include <mutex>

#include <bento4/Ap4.h>
#include <kodi/addon-instance/VideoCodec.h>

class CWVDecrypter;
class CWVCencSingleSampleDecrypter;

class ATTR_DLL_LOCAL CWVCdmAdapter : public media::CdmAdapterClient,
                                     public IWVCdmAdapter<media::CdmAdapter>
{
public:
  CWVCdmAdapter(const DRM::Config& config, CWVDecrypter* host);
  virtual ~CWVCdmAdapter();

  // media::CdmAdapterClient interface methods

  virtual void OnCDMMessage(const char* session,
                            uint32_t session_size,
                            CDMADPMSG msg,
                            const uint8_t* data,
                            size_t data_size,
                            uint32_t status) override;
  virtual cdm::Buffer* AllocateBuffer(size_t sz) override;

  // IWVCdmAdapter interface methods

  std::shared_ptr<media::CdmAdapter> GetCDM() override { return m_cdmAdapter; }
  const DRM::Config& GetConfig() override;
  void SetCodecInstance(void* instance) override;
  void ResetCodecInstance() override;
  std::string_view GetKeySystem() override;
  std::string_view GetLibraryPath() const override;

  // IWVSubject interface methods

  void AttachObserver(IWVObserver* observer) override;
  void DetachObserver(IWVObserver* observer) override;
  void NotifyObservers(const CdmMessage& message) override;

private:
  DRM::Config m_config;
  std::shared_ptr<media::CdmAdapter> m_cdmAdapter;
  kodi::addon::CInstanceVideoCodec* m_codecInstance{nullptr};
  CWVDecrypter* m_host;
  std::list<IWVObserver*> m_observers;
  std::mutex m_observer_mutex;
};
