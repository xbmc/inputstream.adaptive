/*
 *  Copyright (C) 2016 liberty-developer (https://github.com/liberty-developer)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WVDecrypter.h"

#include "decrypters/Helpers.h"
#include "WVCdmAdapter.h"
#include "WVCencSingleSampleDecrypter.h"
#include "utils/Base64Utils.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

#include <kodi/Filesystem.h> //! @todo: cleanup! remove me to use FileUtils.h

#if defined(__linux__) && (defined(__aarch64__) || defined(__arm64__))
#include <dlfcn.h>
#endif

using namespace DRM;
using namespace UTILS;
using namespace kodi::tools;

CWVDecrypter::~CWVDecrypter()
{
#if defined(__linux__) && (defined(__aarch64__) || defined(__arm64__))
  if (m_hdlLibLoader)
    dlclose(m_hdlLibLoader);
#endif
}

bool CWVDecrypter::Initialize()
{
#if defined(__linux__) && (defined(__aarch64__) || defined(__arm64__))
  // On linux arm64, libwidevinecdm.so depends on two dynamic symbols:
  //   __aarch64_ldadd4_acq_rel
  //   __aarch64_swp4_acq_rel
  // These are defined from a separate library cdm_aarch64_loader,
  // but to make them available in the main binary's PLT, we need RTLD_GLOBAL.
  // Kodi kodi::tools::CDllHelper LoadDll() cannot be used because use RTLD_LOCAL,
  // and we need the RTLD_GLOBAL flag.
  std::string binaryPath;
  std::vector<kodi::vfs::CDirEntry> items;
  if (kodi::vfs::GetDirectory(FILESYS::GetAddonPath(), "", items))
  {
    for (auto item : items)
    {
      if (!STRING::Contains(item.Label(), "cdm_aarch64_loader"))
        continue;

      binaryPath = item.Path();
      break;
    }
  }
  if (binaryPath.empty())
  {
    LOG::Log(LOGERROR, "Cannot find the cdm_aarch64_loader file");
    return false;
  }

  m_hdlLibLoader = dlopen(binaryPath.c_str(), RTLD_GLOBAL | RTLD_LAZY);
  if (!m_hdlLibLoader)
  {
    LOG::LogF(LOGERROR, "Failed to load CDM aarch64 loader from path \"%s\", error: %s",
              binaryPath.c_str(), dlerror());
    return false;
  }
#endif
  return true;
}

std::vector<std::string_view> CWVDecrypter::SelectKeySystems(std::string_view keySystem)
{
  std::vector<std::string_view> keySystems;
  if (keySystem == KS_WIDEVINE)
    keySystems.push_back(URN_WIDEVINE);

  return keySystems;
}

bool CWVDecrypter::OpenDRMSystem(const DRM::Config& config)
{
  if (config.license.serverUrl.empty())
  {
    LOG::LogF(LOGERROR, "The DRM license server url has not been specified");
    return false;
  }
  m_WVCdmAdapter = std::make_shared<CWVCdmAdapter>(config, this);

  return m_WVCdmAdapter->GetCDM() != nullptr;
}

std::shared_ptr<Adaptive_CencSingleSampleDecrypter> CWVDecrypter::CreateSingleSampleDecrypter(
    const std::vector<uint8_t>& initData,
    const std::vector<uint8_t>& defaultKeyId,
    std::string_view licenseUrl,
    bool skipSessionMessage,
    CryptoMode cryptoMode)
{
  auto decrypter = std::make_shared<CWVCencSingleSampleDecrypter>(
      m_WVCdmAdapter.get(), initData, defaultKeyId, skipSessionMessage, cryptoMode);
  if (!decrypter->GetSessionId())
  {
    return nullptr;
  }
  return decrypter;
}

void CWVDecrypter::GetCapabilities(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                   const std::vector<uint8_t>& keyId,
                                   uint32_t media,
                                   DRM::DecrypterCapabilites& caps)
{
  if (!decrypter)
  {
    caps = {0, 0, 0};
    return;
  }

  auto wvDecrypter = std::dynamic_pointer_cast<CWVCencSingleSampleDecrypter>(decrypter);
  if (wvDecrypter)
  {
    wvDecrypter->GetCapabilities(keyId, media, caps);
  }
  else
    LOG::LogF(LOGFATAL, "Cannot cast the decrypter shared pointer.");
}

bool CWVDecrypter::HasLicenseKey(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                 const std::vector<uint8_t>& keyId)
{
  auto wvDecrypter = std::dynamic_pointer_cast<CWVCencSingleSampleDecrypter>(decrypter);
  if (wvDecrypter)
  {
    return wvDecrypter->HasKeyId(keyId);
  }
  else
    LOG::LogF(LOGFATAL, "Cannot cast the decrypter shared pointer.");

  return false;
}

std::string CWVDecrypter::GetChallengeB64Data(
    std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter)
{
  auto wvDecrypter = std::dynamic_pointer_cast<CWVCencSingleSampleDecrypter>(decrypter);
  if (wvDecrypter)
  {
    AP4_DataBuffer challengeData = wvDecrypter->GetChallengeData();
    return BASE64::Encode(challengeData.GetData(), challengeData.GetDataSize());
  }
  else
    LOG::LogF(LOGFATAL, "Cannot cast the decrypter shared pointer.");

  return "";
}

bool CWVDecrypter::OpenVideoDecoder(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                    const VIDEOCODEC_INITDATA* initData)
{
  if (!initData)
  {
    LOG::LogF(LOGERROR, "Cannot open video decoder, missing init data");
    return false;
  }

  m_decodingDecrypter = std::dynamic_pointer_cast<CWVCencSingleSampleDecrypter>(decrypter);
  if (m_decodingDecrypter)
  {
    return m_decodingDecrypter->OpenVideoDecoder(initData);
  }
  else
    LOG::LogF(LOGFATAL, "Cannot cast the decrypter shared pointer.");

  return false;
}

VIDEOCODEC_RETVAL CWVDecrypter::DecryptAndDecodeVideo(
    kodi::addon::CInstanceVideoCodec* codecInstance, const DEMUX_PACKET* sample)
{
  if (!m_decodingDecrypter)
    return VC_ERROR;

  return m_decodingDecrypter->DecryptAndDecodeVideo(codecInstance, sample);
}

VIDEOCODEC_RETVAL CWVDecrypter::VideoFrameDataToPicture(
    kodi::addon::CInstanceVideoCodec* codecInstance, VIDEOCODEC_PICTURE* picture)
{
  if (!m_decodingDecrypter)
    return VC_ERROR;

  return m_decodingDecrypter->VideoFrameDataToPicture(codecInstance, picture);
}

void CWVDecrypter::ResetVideo()
{
  if (m_decodingDecrypter)
  {
    m_decodingDecrypter->ResetVideo();
    m_decodingDecrypter = nullptr;
  }
}

void CWVDecrypter::SetLibraryPath(std::string_view libraryPath)
{
  m_libraryPath = libraryPath;
}

bool CWVDecrypter::GetBuffer(void* instance, VIDEOCODEC_PICTURE& picture)
{
  return instance ? static_cast<kodi::addon::CInstanceVideoCodec*>(instance)->GetFrameBuffer(
                        *reinterpret_cast<VIDEOCODEC_PICTURE*>(&picture))
                  : false;
}

void CWVDecrypter::ReleaseBuffer(void* instance, void* buffer)
{
  if (instance)
    static_cast<kodi::addon::CInstanceVideoCodec*>(instance)->ReleaseFrameBuffer(buffer);
}
