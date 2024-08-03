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

#include <kodi/Filesystem.h>

#if defined(__linux__) && (defined(__aarch64__) || defined(__arm64__))
#include <dlfcn.h>
#endif

using namespace DRM;
using namespace UTILS;
using namespace kodi::tools;


CWVDecrypter::~CWVDecrypter()
{
  delete m_WVCdmAdapter;
  m_WVCdmAdapter = nullptr;
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

bool CWVDecrypter::OpenDRMSystem(std::string_view licenseURL,
                                 const std::vector<uint8_t>& serverCertificate,
                                 const uint8_t config)
{
  if (licenseURL.empty())
  {
    LOG::LogF(LOGERROR, "License Key property cannot be empty");
    return false;
  }
  m_WVCdmAdapter = new CWVCdmAdapter(licenseURL, serverCertificate, config, this);

  return m_WVCdmAdapter->GetCdmAdapter() != nullptr;
}

Adaptive_CencSingleSampleDecrypter* CWVDecrypter::CreateSingleSampleDecrypter(
    std::vector<uint8_t>& initData,
    std::string_view optionalKeyParameter,
    const std::vector<uint8_t>& defaultKeyId,
    std::string_view licenseUrl,
    bool skipSessionMessage,
    CryptoMode cryptoMode)
{
  CWVCencSingleSampleDecrypter* decrypter = new CWVCencSingleSampleDecrypter(
      *m_WVCdmAdapter, initData, defaultKeyId, skipSessionMessage, cryptoMode, this);
  if (!decrypter->GetSessionId())
  {
    delete decrypter;
    decrypter = nullptr;
  }
  return decrypter;
}

void CWVDecrypter::DestroySingleSampleDecrypter(Adaptive_CencSingleSampleDecrypter* decrypter)
{
  if (decrypter)
  {
    // close session before dispose
    static_cast<CWVCencSingleSampleDecrypter*>(decrypter)->CloseSessionId();
    delete static_cast<CWVCencSingleSampleDecrypter*>(decrypter);
  }
}

void CWVDecrypter::GetCapabilities(Adaptive_CencSingleSampleDecrypter* decrypter,
                                   const std::vector<uint8_t>& keyId,
                                   uint32_t media,
                                   DecrypterCapabilites& caps)
{
  if (!decrypter)
  {
    caps = {0, 0, 0};
    return;
  }

  static_cast<CWVCencSingleSampleDecrypter*>(decrypter)->GetCapabilities(keyId, media, caps);
}

bool CWVDecrypter::HasLicenseKey(Adaptive_CencSingleSampleDecrypter* decrypter,
                                 const std::vector<uint8_t>& keyId)
{
  if (decrypter)
    return static_cast<CWVCencSingleSampleDecrypter*>(decrypter)->HasKeyId(keyId);
  return false;
}

std::string CWVDecrypter::GetChallengeB64Data(Adaptive_CencSingleSampleDecrypter* decrypter)
{
  if (!decrypter)
    return "";

  AP4_DataBuffer challengeData =
      static_cast<CWVCencSingleSampleDecrypter*>(decrypter)->GetChallengeData();
  return BASE64::Encode(challengeData.GetData(), challengeData.GetDataSize());
}

bool CWVDecrypter::OpenVideoDecoder(Adaptive_CencSingleSampleDecrypter* decrypter,
                                    const VIDEOCODEC_INITDATA* initData)
{
  if (!decrypter || !initData)
    return false;

  m_decodingDecrypter = static_cast<CWVCencSingleSampleDecrypter*>(decrypter);
  return m_decodingDecrypter->OpenVideoDecoder(initData);
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
    m_decodingDecrypter->ResetVideo();
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
