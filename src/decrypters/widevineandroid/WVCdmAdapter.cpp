/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WVCdmAdapter.h"

#include "WVDecrypter.h"
#include "decrypters/HelperWv.h"
#include "decrypters/Helpers.h"
#include "utils/FileUtils.h"
#include "utils/log.h"

#include <jni/src/UUID.h>

using namespace DRM;
using namespace UTILS;

CMediaDrmOnEventListener::CMediaDrmOnEventListener(
    CMediaDrmOnEventCallback* decrypterEventCallback,
    std::shared_ptr<jni::CJNIClassLoader> classLoader)
  : jni::CJNIMediaDrmOnEventListener(classLoader.get())
{
  m_decrypterEventCallback = decrypterEventCallback;
}

void CMediaDrmOnEventListener::onEvent(const jni::CJNIMediaDrm& mediaDrm,
                                       const std::vector<char>& sessionId,
                                       int event,
                                       int extra,
                                       const std::vector<char>& data)
{
  m_decrypterEventCallback->OnMediaDrmEvent(mediaDrm, sessionId, event, extra, data);
}

CWVCdmAdapterA::CWVCdmAdapterA(std::string_view keySystem,
                               const DRM::Config& config,
                               std::shared_ptr<jni::CJNIClassLoader> jniClassLoader,
                               CWVDecrypterA* host)
  : m_keySystem(keySystem), m_config(config), m_host(host)
{
  // The license url come from license_key kodi property
  // we have to kept only the url without the parameters specified after pipe "|" char
  std::string licUrl = m_config.license.serverUri;
  const size_t urlPipePos = licUrl.find('|');
  if (urlPipePos != std::string::npos)
    licUrl.erase(urlPipePos);

  // Build up a CDM path to store decrypter specific stuff, each domain gets it own path
  // the domain name is hashed to generate a short folder name
  std::string drmName = DRM::KeySystemToDrmName(m_keySystem);
  std::string basePath = FILESYS::PathCombine(FILESYS::GetAddonUserPath(), drmName);
  basePath = FILESYS::PathCombine(basePath, DRM::GenerateUrlDomainHash(licUrl));
  basePath += FILESYS::SEPARATOR;
  m_strBasePath = basePath;

  int64_t mostSigBits(0), leastSigBits(0);
  const uint8_t* systemUuid = DRM::KeySystemToUUID(m_keySystem);
  if (!systemUuid)
  {
    LOG::LogF(LOGERROR, "Unable to get the system UUID");
    return;
  }
  for (unsigned int i(0); i < 8; ++i)
    mostSigBits = (mostSigBits << 8) | systemUuid[i];
  for (unsigned int i(8); i < 16; ++i)
    leastSigBits = (leastSigBits << 8) | systemUuid[i];

  jni::CJNIUUID uuid(mostSigBits, leastSigBits);
  m_cdmAdapter = std::make_shared<jni::CJNIMediaDrm>(uuid);
  if (xbmc_jnienv()->ExceptionCheck() || !*m_cdmAdapter.get())
  {
    LOG::LogF(LOGERROR, "Unable to initialize MediaDrm");
    xbmc_jnienv()->ExceptionClear();
    return;
  }

  // Create media drm EventListener (unique_ptr explanation on class comment)
  m_mediaDrmEventListener = std::make_unique<CMediaDrmOnEventListener>(this, jniClassLoader);
  m_cdmAdapter->setOnEventListener(*m_mediaDrmEventListener.get());
  if (xbmc_jnienv()->ExceptionCheck())
  {
    LOG::LogF(LOGERROR, "Exception during installation of EventListener");
    xbmc_jnienv()->ExceptionClear();
    m_cdmAdapter->release();
    return;
  }

  std::vector<uint8_t> strDeviceId = m_cdmAdapter->getPropertyByteArray("deviceUniqueId");
  xbmc_jnienv()->ExceptionClear();
  std::string strSecurityLevel = m_cdmAdapter->getPropertyString("securityLevel");
  xbmc_jnienv()->ExceptionClear();
  std::string strSystemId = m_cdmAdapter->getPropertyString("systemId");
  xbmc_jnienv()->ExceptionClear();


  if (m_keySystem == DRM::KS_WIDEVINE)
  {
    //m_cdmAdapter->setPropertyString("sessionSharing", "enable");
    if (!m_config.license.serverCert.empty())
    {
      m_cdmAdapter->setPropertyByteArray("serviceCertificate", m_config.license.serverCert);
    }
    else
      LoadServiceCertificate();

    if (xbmc_jnienv()->ExceptionCheck())
    {
      LOG::LogF(LOGERROR, "Exception setting Service Certificate");
      xbmc_jnienv()->ExceptionClear();
      m_cdmAdapter->release();
      return;
    }
  }

  LOG::Log(LOGDEBUG,
           "MediaDrm initialized (Device unique ID size: %zu, System ID: %s, Security level: %s)",
           strDeviceId.size(), strSystemId.c_str(), strSecurityLevel.c_str());
}

CWVCdmAdapterA::~CWVCdmAdapterA()
{
  if (m_cdmAdapter)
  {
    m_cdmAdapter->release();
    if (xbmc_jnienv()->ExceptionCheck())
    {
      LOG::LogF(LOGERROR, "Exception releasing media drm");
      xbmc_jnienv()->ExceptionClear();
    }
  }
}

void CWVCdmAdapterA::LoadServiceCertificate()
{
  std::string filename = m_strBasePath + "service_certificate";
  uint8_t* data(nullptr);
  size_t sz(0);
  FILE* f = fopen(filename.c_str(), "rb");

  if (f)
  {
    fseek(f, 0L, SEEK_END);
    sz = ftell(f);
    fseek(f, 0L, SEEK_SET);
    if (sz > 8 && (data = (uint8_t*)malloc(sz)))
      fread(data, 1, sz, f);
    fclose(f);
  }
  if (data)
  {
    auto now = std::chrono::system_clock::now();
    uint64_t certTime = *((uint64_t*)data),
             nowTime =
                 std::chrono::time_point_cast<std::chrono::seconds>(now).time_since_epoch().count();

    if (certTime < nowTime && nowTime - certTime < 86400)
      m_cdmAdapter->setPropertyByteArray("serviceCertificate",
                                       std::vector<uint8_t>(data + 8, data + sz));
    else
      free(data), data = nullptr;
  }
  if (!data)
  {
    LOG::Log(LOGDEBUG, "Requesting new Service Certificate");
    m_cdmAdapter->setPropertyString("privacyMode", "enable");
  }
  else
  {
    LOG::Log(LOGDEBUG, "Use stored Service Certificate");
    free(data), data = nullptr;
  }
}

void CWVCdmAdapterA::OnMediaDrmEvent(const jni::CJNIMediaDrm& mediaDrm,
                                     const std::vector<char>& sessionId,
                                     int event,
                                     int extra,
                                     const std::vector<char>& data)
{
  LOG::Log(LOGDEBUG, "MediaDrm event: type %i arrived", event);

  CdmMessageType type;
  if (event == jni::CJNIMediaDrm::EVENT_KEY_REQUIRED)
    type = CdmMessageType::EVENT_KEY_REQUIRED;
  else
    return;

  CdmMessage cdmMsg;
  cdmMsg.sessionId.assign(sessionId.data(), sessionId.data() + sessionId.size());
  cdmMsg.type = type;
  cdmMsg.data.assign(data.data(), data.data() + data.size());
  cdmMsg.status = extra;

  // Send the message to attached CWVCencSingleSampleDecrypterA instances
  NotifyObservers(cdmMsg);
}

void CWVCdmAdapterA::SaveServiceCertificate()
{
  const std::vector<uint8_t> sc = m_cdmAdapter->getPropertyByteArray("serviceCertificate");
  if (xbmc_jnienv()->ExceptionCheck())
  {
    LOG::LogF(LOGWARNING, "Exception retrieving Service Certificate");
    xbmc_jnienv()->ExceptionClear();
    return;
  }

  if (sc.empty())
  {
    LOG::LogF(LOGWARNING, "Empty Service Certificate");
    return;
  }

  std::string filename = m_strBasePath + "service_certificate";
  FILE* f = fopen(filename.c_str(), "wb");
  if (f)
  {
    auto now = std::chrono::system_clock::now();
    uint64_t nowTime =
        std::chrono::time_point_cast<std::chrono::seconds>(now).time_since_epoch().count();
    fwrite((uint8_t*)&nowTime, 1, sizeof(uint64_t), f);
    fwrite(sc.data(), 1, sc.size(), f);
    fclose(f);
  }
}

const DRM::Config& CWVCdmAdapterA::GetConfig()
{
  return m_config;
}

std::string_view CWVCdmAdapterA::GetKeySystem()
{
  return m_keySystem;
}

std::string_view CWVCdmAdapterA::GetLibraryPath() const
{
  return m_host->GetLibraryPath();
}

void CWVCdmAdapterA::AttachObserver(IWVObserver* observer)
{
  std::lock_guard<std::mutex> lock(m_observer_mutex);
  m_observers.emplace_back(observer);
}

void CWVCdmAdapterA::DetachObserver(IWVObserver* observer)
{
  std::lock_guard<std::mutex> lock(m_observer_mutex);
  m_observers.remove(observer);
}

void CWVCdmAdapterA::NotifyObservers(const CdmMessage& message)
{
  std::lock_guard<std::mutex> lock(m_observer_mutex);
  for (IWVObserver* observer : m_observers)
  {
    if (observer)
      observer->OnNotify(message);
  }
}
