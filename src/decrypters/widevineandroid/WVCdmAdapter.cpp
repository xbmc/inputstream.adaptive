/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WVCdmAdapter.h"

#include "WVDecrypter.h"
#include "decrypters/Helpers.h"
#include "utils/FileUtils.h"
#include "utils/log.h"

#include <jni/src/UUID.h>
#include <kodi/Filesystem.h>

using namespace DRM;
using namespace jni;

CWVCdmAdapterA::CWVCdmAdapterA(WV_KEYSYSTEM ks,
                               std::string_view licenseURL,
                               const std::vector<uint8_t>& serverCert,
                               CJNIMediaDrmOnEventListener* listener,
                               CWVDecrypterA* host)
  : m_keySystem(ks), m_mediaDrm(0), m_licenseUrl(licenseURL), m_host(host)
{
  if (licenseURL.empty())
  {
    LOG::LogF(LOGERROR, "No license URL path specified");
    return;
  }

  std::string drmName;
  if (ks == WIDEVINE)
    drmName = "widevine";
  else if (ks == PLAYREADY)
    drmName = "playready";
  else if (ks == WISEPLAY)
    drmName = "wiseplay";
  else
    drmName = "undefined";

  // The license url come from license_key kodi property
  // we have to kept only the url without the parameters specified after pipe "|" char
  std::string licUrl = m_licenseUrl;
  const size_t urlPipePos = licUrl.find('|');
  if (urlPipePos != std::string::npos)
    licUrl.erase(urlPipePos);

  // Build up a CDM path to store decrypter specific stuff, each domain gets it own path
  // the domain name is hashed to generate a short folder name
  std::string basePath = FILESYS::PathCombine(m_host->GetProfilePath(), drmName);
  basePath = FILESYS::PathCombine(basePath, DRM::GenerateUrlDomainHash(licUrl));
  basePath += FILESYS::SEPARATOR;
  m_strBasePath = basePath;

  int64_t mostSigBits(0), leastSigBits(0);
  const uint8_t* keySystem = GetKeySystem();
  for (unsigned int i(0); i < 8; ++i)
    mostSigBits = (mostSigBits << 8) | keySystem[i];
  for (unsigned int i(8); i < 16; ++i)
    leastSigBits = (leastSigBits << 8) | keySystem[i];

  CJNIUUID uuid(mostSigBits, leastSigBits);
  m_mediaDrm = new CJNIMediaDrm(uuid);
  if (xbmc_jnienv()->ExceptionCheck() || !*m_mediaDrm)
  {
    LOG::LogF(LOGERROR, "Unable to initialize MediaDrm");
    xbmc_jnienv()->ExceptionClear();
    delete m_mediaDrm, m_mediaDrm = nullptr;
    return;
  }

  m_mediaDrm->setOnEventListener(*listener);
  if (xbmc_jnienv()->ExceptionCheck())
  {
    LOG::LogF(LOGERROR, "Exception during installation of EventListener");
    xbmc_jnienv()->ExceptionClear();
    m_mediaDrm->release();
    delete m_mediaDrm, m_mediaDrm = nullptr;
    return;
  }

  std::vector<uint8_t> strDeviceId = m_mediaDrm->getPropertyByteArray("deviceUniqueId");
  xbmc_jnienv()->ExceptionClear();
  std::string strSecurityLevel = m_mediaDrm->getPropertyString("securityLevel");
  xbmc_jnienv()->ExceptionClear();
  std::string strSystemId = m_mediaDrm->getPropertyString("systemId");
  xbmc_jnienv()->ExceptionClear();


  if (m_keySystem == WIDEVINE)
  {
    //m_mediaDrm->setPropertyString("sessionSharing", "enable");
    if (!serverCert.empty())
    {
      m_mediaDrm->setPropertyByteArray("serviceCertificate", serverCert);
    }
    else
      LoadServiceCertificate();

    if (xbmc_jnienv()->ExceptionCheck())
    {
      LOG::LogF(LOGERROR, "Exception setting Service Certificate");
      xbmc_jnienv()->ExceptionClear();
      m_mediaDrm->release();
      delete m_mediaDrm, m_mediaDrm = nullptr;
      return;
    }
  }

  LOG::Log(LOGDEBUG,
           "MediaDrm initialized (Device unique ID size: %zu, System ID: %s, Security level: %s)",
           strDeviceId.size(), strSystemId.c_str(), strSecurityLevel.c_str());

  if (m_licenseUrl.find('|') == std::string::npos)
  {
    if (m_keySystem == WIDEVINE)
      m_licenseUrl += "|Content-Type=application%2Foctet-stream|R{SSM}|";
    else if (m_keySystem == PLAYREADY)
      m_licenseUrl += "|Content-Type=text%2Fxml&SOAPAction=http%3A%2F%2Fschemas.microsoft.com%"
                      "2FDRM%2F2007%2F03%2Fprotocols%2FAcquireLicense|R{SSM}|";
    else
      m_licenseUrl += "|Content-Type=application/json|R{SSM}|";
  }
}

CWVCdmAdapterA::~CWVCdmAdapterA()
{
  if (m_mediaDrm)
  {
    m_mediaDrm->release();
    if (xbmc_jnienv()->ExceptionCheck())
    {
      LOG::LogF(LOGERROR, "Exception releasing media drm");
      xbmc_jnienv()->ExceptionClear();
    }
    delete m_mediaDrm;
    m_mediaDrm = nullptr;
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
      m_mediaDrm->setPropertyByteArray("serviceCertificate",
                                       std::vector<uint8_t>(data + 8, data + sz));
    else
      free(data), data = nullptr;
  }
  if (!data)
  {
    LOG::Log(LOGDEBUG, "Requesting new Service Certificate");
    m_mediaDrm->setPropertyString("privacyMode", "enable");
  }
  else
  {
    LOG::Log(LOGDEBUG, "Use stored Service Certificate");
    free(data), data = nullptr;
  }
}

void CWVCdmAdapterA::SaveServiceCertificate()
{
  const std::vector<uint8_t> sc = m_mediaDrm->getPropertyByteArray("serviceCertificate");
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
