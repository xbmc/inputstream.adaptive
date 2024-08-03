/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WVDecrypter.h"

#include "decrypters/Helpers.h"
#include "WVCencSingleSampleDecrypter.h"
#include "common/AdaptiveDecrypter.h"
#include "jsmn.h"
#include "kodi/tools/StringUtils.h"
#include "utils/Base64Utils.h"
#include "utils/DigestMD5Utils.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"

#include <chrono>
#include <deque>
#include <stdarg.h>
#include <stdlib.h>
#include <thread>

#include <bento4/Ap4.h>
#include <jni/src/ClassLoader.h>
#include <jni/src/UUID.h>
#include <kodi/Filesystem.h>

using namespace DRM;
using namespace UTILS;
using namespace kodi::tools;

namespace
{
kodi::platform::CInterfaceAndroidSystem* ANDROID_SYSTEM{nullptr};
} // unnamed namespace

CMediaDrmOnEventListener::CMediaDrmOnEventListener(CMediaDrmOnEventCallback* decrypterEventCallback,
                                                   CJNIClassLoader* classLoader)
  : CJNIMediaDrmOnEventListener(classLoader), m_decrypterEventCallback(decrypterEventCallback)
{
}

void CMediaDrmOnEventListener::onEvent(const CJNIMediaDrm& mediaDrm,
                                       const std::vector<char>& sessionId,
                                       int event,
                                       int extra,
                                       const std::vector<char>& data)
{
  m_decrypterEventCallback->OnMediaDrmEvent(mediaDrm, sessionId, event, extra, data);
}

CWVDecrypterA::CWVDecrypterA() : m_keySystem(NONE), m_WVCdmAdapter(nullptr)
{
  // CInterfaceAndroidSystem need to be initialized at runtime
  // then we have to set it to global variable just now
  ANDROID_SYSTEM = &m_androidSystem;
};

CWVDecrypterA::~CWVDecrypterA()
{
  delete m_WVCdmAdapter;
  m_WVCdmAdapter = nullptr;

#ifdef DRMTHREAD
  m_jniCondition.notify_one();
  m_jniWorker->join();
  delete m_jniWorker;
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
  std::vector<std::string_view> keySystems;
  if (keySystem == KS_WIDEVINE)
  {
    m_keySystem = WIDEVINE;
    keySystems.push_back(URN_WIDEVINE);
  }
  else if (keySystem == KS_WISEPLAY)
  {
    m_keySystem = WISEPLAY;
    keySystems.push_back(URN_WISEPLAY);
  }
  else if (keySystem == KS_PLAYREADY)
  {
    m_keySystem = PLAYREADY;
    keySystems.push_back(URN_PLAYREADY);
  }
  return keySystems;
}

bool CWVDecrypterA::OpenDRMSystem(std::string_view licenseURL,
                                  const std::vector<uint8_t>& serverCertificate,
                                  const uint8_t config)
{
  if (m_keySystem == NONE)
    return false;

  if (licenseURL.empty())
  {
    LOG::LogF(LOGERROR, "License Key property cannot be empty");
    return false;
  }

  m_WVCdmAdapter = new CWVCdmAdapterA(m_keySystem, licenseURL, serverCertificate,
                                      m_mediaDrmEventListener.get(), this);

  return m_WVCdmAdapter->GetMediaDrm();
}

Adaptive_CencSingleSampleDecrypter* CWVDecrypterA::CreateSingleSampleDecrypter(
    std::vector<uint8_t>& initData,
    std::string_view optionalKeyParameter,
    const std::vector<uint8_t>& defaultKeyId,
    std::string_view licenseUrl,
    bool skipSessionMessage,
    CryptoMode cryptoMode)
{
  CWVCencSingleSampleDecrypterA* decrypter = new CWVCencSingleSampleDecrypterA(
      *m_WVCdmAdapter, initData, optionalKeyParameter, defaultKeyId, this);

  {
    std::lock_guard<std::mutex> lk(m_decrypterListMutex);
    m_decrypterList.push_back(decrypter);
  }

  if (!(*decrypter->GetSessionId() && decrypter->StartSession(skipSessionMessage)))
  {
    DestroySingleSampleDecrypter(decrypter);
    return nullptr;
  }
  return decrypter;
}

void CWVDecrypterA::DestroySingleSampleDecrypter(Adaptive_CencSingleSampleDecrypter* decrypter)
{
  if (decrypter)
  {
    std::vector<CWVCencSingleSampleDecrypterA*>::const_iterator res =
        std::find(m_decrypterList.begin(), m_decrypterList.end(), decrypter);
    if (res != m_decrypterList.end())
    {
      std::lock_guard<std::mutex> lk(m_decrypterListMutex);
      m_decrypterList.erase(res);
    }
    delete static_cast<CWVCencSingleSampleDecrypterA*>(decrypter);
  }
}

void CWVDecrypterA::GetCapabilities(Adaptive_CencSingleSampleDecrypter* decrypter,
                                    const std::vector<uint8_t>& keyId,
                                    uint32_t media,
                                    DecrypterCapabilites& caps)
{
  if (decrypter)
    static_cast<CWVCencSingleSampleDecrypterA*>(decrypter)->GetCapabilities(keyId, media, caps);
  else
    caps = {0, 0, 0};
}

bool CWVDecrypterA::HasLicenseKey(Adaptive_CencSingleSampleDecrypter* decrypter,
                                  const std::vector<uint8_t>& keyId)
{
  if (decrypter)
    return static_cast<CWVCencSingleSampleDecrypterA*>(decrypter)->HasLicenseKey(keyId);
  return false;
}

std::string CWVDecrypterA::GetChallengeB64Data(Adaptive_CencSingleSampleDecrypter* decrypter)
{
  if (!decrypter)
    return "";

  const std::vector<char> data = static_cast<CWVCencSingleSampleDecrypterA*>(decrypter)->GetChallengeData();
  return BASE64::Encode(data);
}

void CWVDecrypterA::OnMediaDrmEvent(const CJNIMediaDrm& mediaDrm,
                                    const std::vector<char>& sessionId,
                                    int event,
                                    int extra,
                                    const std::vector<char>& data)
{
  LOG::LogF(LOGDEBUG, "%d arrived, #decrypter: %lu", event, m_decrypterList.size());
  //we have only one DRM system running (m_WVCdmAdapter) so there is no need to compare mediaDrm
  std::lock_guard<std::mutex> lk(m_decrypterListMutex);
  for (std::vector<CWVCencSingleSampleDecrypterA*>::iterator b(m_decrypterList.begin()),
       e(m_decrypterList.end());
       b != e; ++b)
  {
    if (sessionId.empty() || (*b)->GetSessionIdRaw() == sessionId)
    {
      switch (event)
      {
        case CJNIMediaDrm::EVENT_KEY_REQUIRED:
          (*b)->RequestNewKeys();
          break;
        default:;
      }
    }
    else
    {
      LOG::LogF(LOGDEBUG, "Session does not match: sizes: %lu -> %lu", sessionId.size(),
                (*b)->GetSessionIdRaw().size());
    }
  }
}

bool CWVDecrypterA::Initialize()
{
#ifdef DRMTHREAD
  std::unique_lock<std::mutex> lk(m_jniMutex);
  m_jniWorker = new std::thread(&CWVDecrypterA::JNIThread, this,
                                reinterpret_cast<JavaVM*>(m_androidSystem.GetJNIEnv()));
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
  CJNIClassLoader* classLoader;
  CJNIBase::SetSDKVersion(m_androidSystem.GetSDKVersion());
  CJNIBase::SetBaseClassName(m_androidSystem.GetClassName());
  LOG::Log(LOGDEBUG, "WVDecrypter JNI, SDK version: %d", m_androidSystem.GetSDKVersion());

  const char* apkEnv = getenv("XBMC_ANDROID_APK");
  if (!apkEnv)
    apkEnv = getenv("KODI_ANDROID_APK");

  if (!apkEnv)
    return false;

  std::string apkPath = apkEnv;

  //! @todo: make classLoader a smartpointer
  classLoader = new CJNIClassLoader(apkPath);
  if (xbmc_jnienv()->ExceptionCheck())
  {
    LOG::LogF(LOGERROR, "Failed to create ClassLoader");
    xbmc_jnienv()->ExceptionDescribe();
    xbmc_jnienv()->ExceptionClear();

    delete classLoader, classLoader = nullptr;

    return false;
  }

  m_classLoader = classLoader;
  m_mediaDrmEventListener = std::make_unique<CMediaDrmOnEventListener>(this, m_classLoader);
  return true;
}

// Definition for the xbmc_jnienv method of the jni utils (jutils.hpp)
JNIEnv* xbmc_jnienv()
{
  return static_cast<JNIEnv*>(ANDROID_SYSTEM->GetJNIEnv());
}
