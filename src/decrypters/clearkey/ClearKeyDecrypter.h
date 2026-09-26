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

  virtual const std::string GetName() const override { return "ClearKey-SW"; }

  virtual bool IsKeySystemSupported(std::string_view keySystem) override;

  virtual std::shared_ptr<Adaptive_CencSingleSampleDecrypter> CreateSingleSampleDecrypter(
      const DRM::Config& config,
      const std::vector<uint8_t>& defaultkeyid,
      CryptoMode cryptoMode) override;

  virtual void GetCapabilities(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                               const std::vector<uint8_t>& keyid,
                               DRM::Capabilities& caps,
                               DRMMediaType mediaType) override
  {
  }
  virtual std::optional<bool> HasLicenseKey(
      std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
      const std::vector<uint8_t>& keyid) override;

  virtual std::string GetChallengeB64Data(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter) override
  {
    return "";
  }
  virtual bool OpenVideoDecoder(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                const VIDEOCODEC_INITDATA* initData) override
  {
    return false;
  }
  virtual bool OpenAudioDecoder(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                const AUDIOCODEC_INITDATA* initData) override
  {
    return false;
  }
  virtual VIDEOCODEC_RETVAL DecryptAndDecodeVideo(kodi::addon::CInstanceVideoCodec* hostInstance,
                                                  const DEMUX_PACKET* sample) override
  {
    return VC_NONE;
  }
  virtual AUDIOCODEC_RETVAL DecryptAndDecodeAudio(kodi::addon::CInstanceAudioCodec* hostInstance,
                                                  const DEMUX_PACKET* sample) override
  {
    return AC_NONE;
  }
  virtual VIDEOCODEC_RETVAL VideoFrameDataToPicture(kodi::addon::CInstanceVideoCodec* hostInstance,
                                                    VIDEOCODEC_PICTURE* picture) override
  {
    return VC_NONE;
  }
  virtual AUDIOCODEC_RETVAL AudioFrameDataToFrame(kodi::addon::CInstanceAudioCodec* codecInstance,
                                                  AUDIOCODEC_FRAME* frame) override
  {
    return AC_NONE;
  }
  virtual void ResetVideo() override {}
  virtual void ResetAudio() override {}
  virtual void SetLibraryPath(std::string_view libraryPath) override
  {
    m_libraryPath = libraryPath;
  }

  virtual bool GetBuffer(void* instance, VIDEOCODEC_PICTURE& picture) { return false; }
  virtual void ReleaseBuffer(void* instance, void* buffer) {}
  virtual std::string_view GetLibraryPath() const override { return m_libraryPath; }

private:
  DRM::Config m_config;
  std::string m_libraryPath;
};
