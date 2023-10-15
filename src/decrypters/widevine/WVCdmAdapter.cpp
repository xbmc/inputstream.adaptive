/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "utils/log.h"
#include "CdmFixedBuffer.h"
#include "WVCencSingleSampleDecrypter.h"
#include "WVCdmAdapter.h"
#include "WVDecrypter.h"

#include <kodi/Filesystem.h>

namespace
{
#if WIN32
  constexpr const char* LIBRARY_FILENAME = "widevinecdm.dll";
#elif TARGET_DARWIN_EMBEDDED
  constexpr const char* LIBRARY_FILENAME = "libwidevinecdm.dylib";
#else
  constexpr const char* LIBRARY_FILENAME = "libwidevinecdm.so";
#endif
} // unnamed namespace

CWVCdmAdapter::CWVCdmAdapter(std::string_view licenseURL,
                             const std::vector<uint8_t>& serverCert,
                             const uint8_t config,
                             CWVDecrypter* host)
  : m_licenseUrl(licenseURL), m_host(host), m_codecInstance(nullptr)
{
  std::string strLibPath = m_host->GetLibraryPath();
  if (strLibPath.empty())
  {
    LOG::LogF(LOGERROR, "No Widevine library path specified in settings");
    return;
  }
  strLibPath += LIBRARY_FILENAME;

  std::string strBasePath = m_host->GetProfilePath();
  char cSep = strBasePath.back();
  strBasePath += "widevine";
  strBasePath += cSep;
  kodi::vfs::CreateDirectory(strBasePath.c_str());

  //Build up a CDM path to store decrypter specific stuff. Each domain gets it own path
  const char* bspos(strchr(m_licenseUrl.c_str(), ':'));
  if (!bspos || bspos[1] != '/' || bspos[2] != '/' || !(bspos = strchr(bspos + 3, '/')))
  {
    LOG::LogF(LOGERROR, "Unable to find protocol inside license URL");
    return;
  }
  if (bspos - m_licenseUrl.c_str() > 256)
  {
    LOG::Log(LOGERROR, "Length of license URL domain exeeds max. size of 256");
    return;
  }
  char buffer[1024];
  buffer[(bspos - m_licenseUrl.c_str()) * 2] = 0;
  AP4_FormatHex(reinterpret_cast<const uint8_t*>(m_licenseUrl.c_str()),
                bspos - m_licenseUrl.c_str(), buffer);

  strBasePath += buffer;
  strBasePath += cSep;
  kodi::vfs::CreateDirectory(strBasePath.c_str());

  wv_adapter = std::shared_ptr<media::CdmAdapter>(new media::CdmAdapter(
      "com.widevine.alpha", strLibPath, strBasePath,
      media::CdmConfig(false, (config & DRM::IDecrypter::CONFIG_PERSISTENTSTORAGE) != 0),
      dynamic_cast<media::CdmAdapterClient*>(this)));
  if (!wv_adapter->valid())
  {
    LOG::Log(LOGERROR, "Unable to load widevine shared library (%s)", strLibPath.c_str());
    wv_adapter = nullptr;
    return;
  }

  if (!serverCert.empty())
    wv_adapter->SetServerCertificate(0, serverCert.data(), serverCert.size());

  // For backward compatibility: If no | is found in URL, use the most common working config
  if (m_licenseUrl.find('|') == std::string::npos)
    m_licenseUrl += "|Content-Type=application%2Foctet-stream|R{SSM}|";

  //wv_adapter->GetStatusForPolicy();
  //wv_adapter->QueryOutputProtectionStatus();
}

CWVCdmAdapter::~CWVCdmAdapter()
{
  if (wv_adapter)
  {
    wv_adapter->RemoveClient();
    LOG::Log(LOGERROR, "Instances: %u", wv_adapter.use_count());
    wv_adapter = nullptr;
  }
}

void CWVCdmAdapter::OnCDMMessage(const char* session,
                          uint32_t session_size,
                          CDMADPMSG msg,
                          const uint8_t* data,
                          size_t data_size,
                          uint32_t status)
{
  LOG::Log(LOGDEBUG, "CDMMessage: %u arrived!", msg);
  std::vector<CWVCencSingleSampleDecrypter*>::iterator b(ssds.begin()), e(ssds.end());
  for (; b != e; ++b)
    if (!(*b)->GetSessionId() || strncmp((*b)->GetSessionId(), session, session_size) == 0)
      break;

  if (b == ssds.end())
    return;

  if (msg == CDMADPMSG::kSessionMessage)
  {
    (*b)->SetSession(session, session_size, data, data_size);
  }
  else if (msg == CDMADPMSG::kSessionKeysChange)
    (*b)->AddSessionKey(data, data_size, status);
}

cdm::Buffer* CWVCdmAdapter::AllocateBuffer(size_t sz)
{
  VIDEOCODEC_PICTURE pic;
  pic.decodedDataSize = sz;
  if (m_host->GetBuffer(m_codecInstance, pic))
  {
    CdmFixedBuffer* buf = new CdmFixedBuffer;
    buf->initialize(m_codecInstance, pic.decodedData, pic.decodedDataSize, pic.videoBufferHandle, m_host);
    return buf;
  }
  return nullptr;
  ;
}
