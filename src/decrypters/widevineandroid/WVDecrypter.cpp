/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WVDecrypter.h"

#include "WVCdmAdapter.h"
#include "WVCencSingleSampleDecrypter.h"
#include "common/AdaptiveDecrypter.h"
#include "decrypters/Helpers.h"
#include "jsmn.h"
#include "utils/Base64Utils.h"
#include "utils/log.h"

#include <jni/src/ClassLoader.h>
#include <jni/src/UUID.h>

using namespace DRM;
using namespace UTILS;

namespace
{
kodi::platform::CInterfaceAndroidSystem* ANDROID_SYSTEM{nullptr};
} // unnamed namespace

CWVDecrypterA::CWVDecrypterA()
{
  // CInterfaceAndroidSystem need to be initialized at runtime
  // then we have to set it to global variable just now
  ANDROID_SYSTEM = &m_androidSystem;
};

CWVDecrypterA::~CWVDecrypterA()
{
#ifdef DRMTHREAD
  m_jniCondition.notify_one();
  m_jniWorker->join();
#endif
};

#ifdef DRMTHREAD
void JNIThread(JavaVM* vm)
{
  m_jniCondition.notify_one();
  std::unique_lock<std::mutex> lk(m_jniMutex);
  m_jniCondition.wait(lk);

  LOG::Log(SSDDEBUG, "JNI thread terminated");
}
#endif

std::vector<std::string_view> CWVDecrypterA::SelectKeySystems(std::string_view keySystem)
{
  LOG::Log(LOGDEBUG, "Key system request: %s", keySystem);

  if (keySystem == KS_WIDEVINE)
  {
    m_keySystem = keySystem;
    return {URN_WIDEVINE};
  }
  else if (keySystem == KS_WISEPLAY)
  {
    m_keySystem = keySystem;
    return {URN_WISEPLAY};
  }
  else if (keySystem == KS_PLAYREADY)
  {
    m_keySystem = keySystem;
    return {URN_PLAYREADY};
  }

  m_keySystem.clear();
  return {};
}

bool CWVDecrypterA::OpenDRMSystem(std::string_view licenseURL,
                                  const std::vector<uint8_t>& serverCertificate,
                                  const uint8_t config)
{
  if (m_keySystem.empty())
    return false;

  if (licenseURL.empty())
  {
    LOG::LogF(LOGERROR, "License Key property cannot be empty");
    return false;
  }

  m_WVCdmAdapter = std::make_shared<CWVCdmAdapterA>(m_keySystem, licenseURL, serverCertificate,
                                                    m_classLoader, this);

  return m_WVCdmAdapter->GetCDM() != nullptr;
}

std::shared_ptr<Adaptive_CencSingleSampleDecrypter> CWVDecrypterA::CreateSingleSampleDecrypter(
    std::vector<uint8_t>& initData,
    std::string_view optionalKeyParameter,
    const std::vector<uint8_t>& defaultKeyId,
    std::string_view licenseUrl,
    bool skipSessionMessage,
    CryptoMode cryptoMode)
{
  std::shared_ptr<CWVCencSingleSampleDecrypterA> decrypter =
      std::make_shared<CWVCencSingleSampleDecrypterA>(m_WVCdmAdapter.get(), initData,
                                                      optionalKeyParameter, defaultKeyId);

  if (!(*decrypter->GetSessionId() && decrypter->StartSession(skipSessionMessage)))
  {
    return nullptr;
  }
  return decrypter;
}

void CWVDecrypterA::GetCapabilities(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                    const std::vector<uint8_t>& keyId,
                                    uint32_t media,
                                    DRM::DecrypterCapabilites& caps)
{
  if (!decrypter)
  {
    caps = {0, 0, 0};
    return;
  }

  auto wvDecrypter = std::dynamic_pointer_cast<CWVCencSingleSampleDecrypterA>(decrypter);
  if (wvDecrypter)
  {
    wvDecrypter->GetCapabilities(keyId, media, caps);
  }
  else
    LOG::LogF(LOGFATAL, "Cannot cast the decrypter shared pointer.");
}

bool CWVDecrypterA::HasLicenseKey(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                  const std::vector<uint8_t>& keyId)
{
  auto wvDecrypter = std::dynamic_pointer_cast<CWVCencSingleSampleDecrypterA>(decrypter);
  if (wvDecrypter)
  {
    return wvDecrypter->HasLicenseKey(keyId);
  }
  else
    LOG::LogF(LOGFATAL, "Cannot cast the decrypter shared pointer.");

  return false;
}

std::string CWVDecrypterA::GetChallengeB64Data(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter)
{
  auto wvDecrypter = std::dynamic_pointer_cast<CWVCencSingleSampleDecrypterA>(decrypter);
  if (wvDecrypter)
  {
    const std::vector<char> challengeData = wvDecrypter->GetChallengeData();
    return BASE64::Encode(challengeData);
  }
  else
    LOG::LogF(LOGFATAL, "Cannot cast the decrypter shared pointer.");

  return "";
}

bool CWVDecrypterA::Initialize()
{
#ifdef DRMTHREAD
  std::unique_lock<std::mutex> lk(m_jniMutex);
  m_jniWorker = std::make_unique<std::thread>(
      &CWVDecrypterA::JNIThread, this, reinterpret_cast<JavaVM*>(m_androidSystem.GetJNIEnv()));
  m_jniCondition.wait(lk);
#endif
  if (xbmc_jnienv()->ExceptionCheck())
  {
    LOG::LogF(LOGERROR, "Failed to load MediaDrmOnEventListener");
    xbmc_jnienv()->ExceptionDescribe();
    xbmc_jnienv()->ExceptionClear();
    return false;
  }

  //JNIEnv* env = static_cast<JNIEnv*>(m_androidSystem.GetJNIEnv());
  CJNIBase::SetSDKVersion(m_androidSystem.GetSDKVersion());
  CJNIBase::SetBaseClassName(m_androidSystem.GetClassName());
  LOG::Log(LOGDEBUG, "WVDecrypter JNI, SDK version: %d", m_androidSystem.GetSDKVersion());

  const char* apkEnv = getenv("XBMC_ANDROID_APK");
  if (!apkEnv)
    apkEnv = getenv("KODI_ANDROID_APK");

  if (!apkEnv)
  {
    LOG::LogF(LOGERROR, "Cannot get enviroment XBMC_ANDROID_APK/KODI_ANDROID_APK value");
    return false;
  }

  std::string apkPath = apkEnv;

  m_classLoader = std::make_shared<jni::CJNIClassLoader>(apkEnv);
  if (xbmc_jnienv()->ExceptionCheck())
  {
    LOG::LogF(LOGERROR, "Failed to create ClassLoader");
    xbmc_jnienv()->ExceptionDescribe();
    xbmc_jnienv()->ExceptionClear();

    return false;
  }

  return true;
}

// Definition for the xbmc_jnienv method of the jni utils (jutils.hpp)
JNIEnv* xbmc_jnienv()
{
  return static_cast<JNIEnv*>(ANDROID_SYSTEM->GetJNIEnv());
}
