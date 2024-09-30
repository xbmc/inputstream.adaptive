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

#ifdef DRMTHREAD
#include <thread>
#endif

#include <kodi/platform/android/System.h>

class CWVCdmAdapterA;
namespace jni
{
class CJNIClassLoader;
}

class ATTR_DLL_LOCAL CWVDecrypterA : public DRM::IDecrypter
{
public:
  CWVDecrypterA();
  ~CWVDecrypterA();

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

  virtual std::vector<std::string_view> SelectKeySystems(std::string_view keySystem) override;

  virtual bool OpenDRMSystem(const DRM::Config& config) override;

  virtual std::shared_ptr<Adaptive_CencSingleSampleDecrypter> CreateSingleSampleDecrypter(
      std::vector<uint8_t>& initData,
      const std::vector<uint8_t>& defaultKeyId,
      std::string_view licenseUrl,
      bool skipSessionMessage,
      CryptoMode cryptoMode) override;

  virtual void GetCapabilities(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                               const std::vector<uint8_t>& keyId,
                               uint32_t media,
                               DRM::DecrypterCapabilites& caps) override;

  virtual bool HasLicenseKey(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                             const std::vector<uint8_t>& keyId) override;

  virtual std::string GetChallengeB64Data(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter) override;

  virtual bool IsInitialised() override { return m_WVCdmAdapter != nullptr; }

  virtual bool OpenVideoDecoder(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                const VIDEOCODEC_INITDATA* initData) override
  {
    return false;
  }

  virtual VIDEOCODEC_RETVAL DecryptAndDecodeVideo(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                  const DEMUX_PACKET* sample) override
  {
    return VC_ERROR;
  }

  virtual VIDEOCODEC_RETVAL VideoFrameDataToPicture(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                    VIDEOCODEC_PICTURE* picture) override
  {
    return VC_ERROR;
  }

  virtual void ResetVideo() override {}

  virtual void SetLibraryPath(std::string_view libraryPath) override
  {
    m_libraryPath = libraryPath;
  }
  virtual std::string_view GetLibraryPath() const override { return m_libraryPath; }

private:
  std::string m_libraryPath;
  kodi::platform::CInterfaceAndroidSystem m_androidSystem;
  std::string m_keySystem;
  std::shared_ptr<CWVCdmAdapterA> m_WVCdmAdapter;
  std::shared_ptr<jni::CJNIClassLoader> m_classLoader;
  std::string m_retvalHelper;
#ifdef DRMTHREAD
  std::mutex m_jniMutex;
  std::condition_variable m_jniCondition;
  std::unique_ptr<std::thread> m_jniWorker;
#endif
};
