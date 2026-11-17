/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "decrypters/IDecrypter.h"

#include <memory>
#include <mutex>
#include <unordered_map>

#ifdef DRMTHREAD
#include <thread>
#endif

#include <kodi/platform/android/System.h>

class CWVCdmAdapterA;
class CJNIClassLoader;


class ATTR_DLL_LOCAL CWVDecrypterA : public DRM::IDecrypter
{
public:
  CWVDecrypterA();
  ~CWVDecrypterA();

  virtual const std::string GetName() const override { return "Widevine"; }

  bool Initialize() override;

#ifdef DRMTHREAD
  void JNIThread(JavaVM* vm)
  {
    m_jniCondition.notify_one();
    std::unique_lock<std::mutex> lk(m_jniMutex);
    m_jniCondition.wait(lk);

    LOG::Log(SSDDEBUG, "JNI thread terminated");
  }
#endif

  virtual bool IsKeySystemSupported(std::string_view keySystem) override;

  virtual std::shared_ptr<Adaptive_CencSingleSampleDecrypter> CreateSingleSampleDecrypter(
      const DRM::Config& config,
      const std::vector<uint8_t>& defaultKeyId,
      CryptoMode cryptoMode) override;

  virtual void GetCapabilities(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                               const std::vector<uint8_t>& keyId,
                               DRM::Capabilities& caps,
                               DRM::DRMMediaType mediaType) override;

  virtual std::optional<bool> HasLicenseKey(
      std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
      const std::vector<uint8_t>& keyId) override;

  virtual std::string GetChallengeB64Data(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter) override;

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

  virtual VIDEOCODEC_RETVAL DecryptAndDecodeVideo(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                  const DEMUX_PACKET* sample) override
  {
    return VC_ERROR;
  }

  virtual AUDIOCODEC_RETVAL DecryptAndDecodeAudio(kodi::addon::CInstanceAudioCodec* hostInstance,
                                                  const DEMUX_PACKET* sample) override
  {
    return AC_NONE;
  }

  virtual VIDEOCODEC_RETVAL VideoFrameDataToPicture(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                    VIDEOCODEC_PICTURE* picture) override
  {
    return VC_ERROR;
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
  virtual std::string_view GetLibraryPath() const override { return m_libraryPath; }

private:
  std::string m_libraryPath;
  kodi::platform::CInterfaceAndroidSystem m_androidSystem;
  std::unordered_map<std::string, std::shared_ptr<CWVCdmAdapterA>> m_wvAdapters; // KeySystem - Adapter instance
  std::shared_ptr<CJNIClassLoader> m_classLoader;
  std::string m_retvalHelper;
#ifdef DRMTHREAD
  std::mutex m_jniMutex;
  std::condition_variable m_jniCondition;
  std::unique_ptr<std::thread> m_jniWorker;
#endif
};
