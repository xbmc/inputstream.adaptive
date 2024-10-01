/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WVCdmAdapter.h"

#include "CdmFixedBuffer.h"
#include "WVCencSingleSampleDecrypter.h"
#include "WVDecrypter.h"
#include "decrypters/Helpers.h"
#include "utils/FileUtils.h"
#include "utils/log.h"

#include <kodi/Filesystem.h>

using namespace UTILS;

namespace
{
#if WIN32
constexpr const char* LIBRARY_FILENAME = "widevinecdm.dll";
#elif TARGET_DARWIN
constexpr const char* LIBRARY_FILENAME = "libwidevinecdm.dylib";
#else
constexpr const char* LIBRARY_FILENAME = "libwidevinecdm.so";
#endif
} // unnamed namespace

CWVCdmAdapter::CWVCdmAdapter(const DRM::Config& config, CWVDecrypter* host)
  : m_config(config), m_host(host)
{
  if (m_host->GetLibraryPath().empty())
  {
    LOG::LogF(LOGERROR, "Widevine CDM library path not specified");
    return;
  }
  std::string cdmPath = FILESYS::PathCombine(m_host->GetLibraryPath(), LIBRARY_FILENAME);

  // The license url come from license_key kodi property
  // we have to kept only the url without the parameters specified after pipe "|" char
  std::string licUrl = m_config.license.serverUrl;
  const size_t urlPipePos = licUrl.find('|');
  if (urlPipePos != std::string::npos)
    licUrl.erase(urlPipePos);

  // Build up a CDM path to store decrypter specific stuff, each domain gets it own path
  // the domain name is hashed to generate a short folder name
  std::string basePath = FILESYS::PathCombine(FILESYS::GetAddonUserPath(), "widevine");
  basePath = FILESYS::PathCombine(basePath, DRM::GenerateUrlDomainHash(licUrl));
  basePath += FILESYS::SEPARATOR;

  m_cdmAdapter =
      std::make_shared<media::CdmAdapter>("com.widevine.alpha", cdmPath, basePath,
                                          media::CdmConfig(false, m_config.isPersistentStorage),
                                          dynamic_cast<media::CdmAdapterClient*>(this));

  if (!m_cdmAdapter->valid())
  {
    LOG::Log(LOGERROR, "Unable to load widevine shared library (%s)", cdmPath.c_str());
    m_cdmAdapter = nullptr;
    return;
  }

  const std::vector<uint8_t>& cert = m_config.license.serverCert;
  if (!cert.empty())
  {
    m_cdmAdapter->SetServerCertificate(0, cert.data(), cert.size());
  }

  // m_cdmAdapter->GetStatusForPolicy();
  // m_cdmAdapter->QueryOutputProtectionStatus();
}

CWVCdmAdapter::~CWVCdmAdapter()
{
  if (m_cdmAdapter)
  {
    m_cdmAdapter->RemoveClient();
    // LOG::LogF(LOGDEBUG, "CDM Adapter instances: %u", m_cdmAdapter.use_count());
    m_cdmAdapter = nullptr;
  }
}

void CWVCdmAdapter::OnCDMMessage(const char* session,
                          uint32_t session_size,
                          CDMADPMSG msg,
                          const uint8_t* data,
                          size_t data_size,
                          uint32_t status)
{
  LOG::Log(LOGDEBUG, "CDM message: type %i arrived", msg);

  CdmMessageType type;
  if (msg == CDMADPMSG::kSessionMessage)
    type = CdmMessageType::SESSION_MESSAGE;
  else if (msg == CDMADPMSG::kSessionKeysChange)
    type = CdmMessageType::SESSION_KEY_CHANGE;
  else
    return;

  CdmMessage cdmMsg;
  cdmMsg.sessionId.assign(session, session + session_size);
  cdmMsg.type = type;
  cdmMsg.data.assign(data, data + data_size);
  cdmMsg.status = status;

  // Send the message to attached CWVCencSingleSampleDecrypter instances
  NotifyObservers(cdmMsg);
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
}

const DRM::Config& CWVCdmAdapter::GetConfig()
{
  return m_config;
}

void CWVCdmAdapter::SetCodecInstance(void* instance)
{
  m_codecInstance = reinterpret_cast<kodi::addon::CInstanceVideoCodec*>(instance);
}

void CWVCdmAdapter::ResetCodecInstance()
{
  m_codecInstance = nullptr;
}

std::string_view CWVCdmAdapter::GetKeySystem()
{
  return KS_WIDEVINE;
}

std::string_view CWVCdmAdapter::GetLibraryPath() const
{
  return m_host->GetLibraryPath();
}

void CWVCdmAdapter::AttachObserver(IWVObserver* observer)
{
  std::lock_guard<std::mutex> lock(m_observer_mutex);
  m_observers.emplace_back(observer);
}

void CWVCdmAdapter::DetachObserver(IWVObserver* observer)
{
  std::lock_guard<std::mutex> lock(m_observer_mutex);
  m_observers.remove(observer);
}

void CWVCdmAdapter::NotifyObservers(const CdmMessage& message)
{
  std::lock_guard<std::mutex> lock(m_observer_mutex);
  for (IWVObserver* observer : m_observers)
  {
    if (observer)
      observer->OnNotify(message);
  }
}
