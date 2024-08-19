/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../IDecrypter.h"
#include "WVCdmAdapter.h"
#include "utils/Base64Utils.h"
#include "utils/log.h"

#include <memory>
#include <mutex>

#include <jni/src/MediaDrm.h>
#include <jni/src/MediaDrmOnEventListener.h>
#include <kodi/platform/android/System.h>

class CWVCencSingleSampleDecrypterA;

using namespace UTILS;
using namespace DRM;
using namespace jni;

class CMediaDrmOnEventCallback
{
public:
  CMediaDrmOnEventCallback() = default;
  virtual ~CMediaDrmOnEventCallback() = default;

  virtual void OnMediaDrmEvent(const CJNIMediaDrm& mediaDrm,
                               const std::vector<char>& sessionId,
                               int event,
                               int extra,
                               const std::vector<char>& data) = 0;
};

/*!
 * \brief This class is derived from CJNIMediaDrmOnEventListener to allow us
 *        to initialize the base class at later time, since the constructors
 *        of CJNIMediaDrmOnEventListener need to access to the global
 *        xbmc_jnienv method immediately.
 */
class CMediaDrmOnEventListener : public CJNIMediaDrmOnEventListener
{
public:
  CMediaDrmOnEventListener(CMediaDrmOnEventCallback* decrypterEventCallback,
                           CJNIClassLoader* classLoader);
  virtual ~CMediaDrmOnEventListener() = default;

  virtual void onEvent(const CJNIMediaDrm& mediaDrm,
                       const std::vector<char>& sessionId,
                       int event,
                       int extra,
                       const std::vector<char>& data) override;

private:
  CMediaDrmOnEventCallback* m_decrypterEventCallback;
};

class ATTR_DLL_LOCAL CWVDecrypterA : public IDecrypter, public CMediaDrmOnEventCallback
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

  virtual bool OpenDRMSystem(std::string_view licenseURL,
                             const std::vector<uint8_t>& serverCertificate,
                             const uint8_t config) override;

  virtual Adaptive_CencSingleSampleDecrypter* CreateSingleSampleDecrypter(
      std::vector<uint8_t>& initData,
      std::string_view optionalKeyParameter,
      const std::vector<uint8_t>& defaultKeyId,
      std::string_view licenseUrl,
      bool skipSessionMessage,
      CryptoMode cryptoMode) override;

  virtual void DestroySingleSampleDecrypter(Adaptive_CencSingleSampleDecrypter* decrypter) override;

  virtual void GetCapabilities(Adaptive_CencSingleSampleDecrypter* decrypter,
                               const std::vector<uint8_t>& keyId,
                               uint32_t media,
                               DecrypterCapabilites& caps) override;

  virtual bool HasLicenseKey(Adaptive_CencSingleSampleDecrypter* decrypter,
                             const std::vector<uint8_t>& keyId) override;

  virtual std::string GetChallengeB64Data(Adaptive_CencSingleSampleDecrypter* decrypter) override;

  virtual bool IsInitialised() override { return m_WVCdmAdapter != nullptr; }

  virtual bool OpenVideoDecoder(Adaptive_CencSingleSampleDecrypter* decrypter,
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

  virtual void OnMediaDrmEvent(const CJNIMediaDrm& mediaDrm,
                               const std::vector<char>& sessionId,
                               int event,
                               int extra,
                               const std::vector<char>& data) override;

private:
  std::string m_libraryPath;
  kodi::platform::CInterfaceAndroidSystem m_androidSystem;
  std::unique_ptr<CMediaDrmOnEventListener> m_mediaDrmEventListener;
  WV_KEYSYSTEM m_keySystem;
  CWVCdmAdapterA* m_WVCdmAdapter;
  inline static CJNIClassLoader* m_classLoader{nullptr};
  std::vector<CWVCencSingleSampleDecrypterA*> m_decrypterList;
  std::mutex m_decrypterListMutex;
  std::string m_retvalHelper;
#ifdef DRMTHREAD
  std::mutex m_jniMutex;
  std::condition_variable m_jniCondition;
  std::thread* m_jniWorker;
#endif
};
