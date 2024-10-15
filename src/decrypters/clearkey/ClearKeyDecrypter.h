/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once
#include "decrypters/IDecrypter.h"

using namespace DRM;

class CClearKeyDecrypter : public IDecrypter
{
public:
  CClearKeyDecrypter(){};
  virtual ~CClearKeyDecrypter() override{};
  virtual std::vector<std::string_view> SelectKeySystems(std::string_view keySystem) override;
  virtual bool OpenDRMSystem(const DRM::Config& config) override;
  virtual std::shared_ptr<Adaptive_CencSingleSampleDecrypter> CreateSingleSampleDecrypter(
      const std::vector<uint8_t>& initData,
      const std::vector<uint8_t>& defaultkeyid,
      std::string_view licenseUrl,
      bool skipSessionMessage,
      CryptoMode cryptoMode) override;

  virtual void GetCapabilities(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                               const std::vector<uint8_t>& keyid,
                               uint32_t media,
                               DRM::DecrypterCapabilites& caps) override
  {
  }
  virtual bool HasLicenseKey(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                             const std::vector<uint8_t>& keyid) override;
  virtual bool IsInitialised() override { return m_isInitialized; }
  virtual std::string GetChallengeB64Data(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter) override
  {
    return "";
  }
  virtual bool OpenVideoDecoder(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                const VIDEOCODEC_INITDATA* initData) override
  {
    return false;
  }
  virtual VIDEOCODEC_RETVAL DecryptAndDecodeVideo(kodi::addon::CInstanceVideoCodec* hostInstance,
                                                  const DEMUX_PACKET* sample) override
  {
    return VC_NONE;
  }
  virtual VIDEOCODEC_RETVAL VideoFrameDataToPicture(kodi::addon::CInstanceVideoCodec* hostInstance,
                                                    VIDEOCODEC_PICTURE* picture) override
  {
    return VC_NONE;
  }
  virtual void ResetVideo() override {}
  virtual void SetLibraryPath(std::string_view libraryPath) override
  {
    m_libraryPath = libraryPath;
  }

  virtual bool GetBuffer(void* instance, VIDEOCODEC_PICTURE& picture) { return false; }
  virtual void ReleaseBuffer(void* instance, void* buffer) {}
  virtual std::string_view GetLibraryPath() const override { return m_libraryPath; }

private:
  bool m_isInitialized{false};
  DRM::Config m_config;
  std::string m_libraryPath;
};
