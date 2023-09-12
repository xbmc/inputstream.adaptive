/*
 *  Copyright (C) 2016 liberty-developer (https://github.com/liberty-developer)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WVDecrypter.h"

#include "WVCdmAdapter.h"
#include "WVCencSingleSampleDecrypter.h"
#include "utils/Base64Utils.h"
#include "utils/log.h"

#include <kodi/Filesystem.h>

using namespace DRM;
using namespace UTILS;
using namespace kodi::tools;


CWVDecrypter::~CWVDecrypter()
{
  delete m_WVCdmAdapter;
  m_WVCdmAdapter = nullptr;
}

const char* CWVDecrypter::SelectKeySytem(const char* keySystem)
{
  if (strcmp(keySystem, "com.widevine.alpha"))
    return nullptr;

  return "urn:uuid:EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED";
}

bool CWVDecrypter::OpenDRMSystem(const char* licenseURL,
                                 const AP4_DataBuffer& serverCertificate,
                                 const uint8_t config)
{
  m_WVCdmAdapter = new CWVCdmAdapter(licenseURL, serverCertificate, config, this);

  return m_WVCdmAdapter->GetCdmAdapter() != nullptr;
}

Adaptive_CencSingleSampleDecrypter* CWVDecrypter::CreateSingleSampleDecrypter(
    AP4_DataBuffer& pssh,
    const char* optionalKeyParameter,
    std::string_view defaultKeyId,
    bool skipSessionMessage,
    CryptoMode cryptoMode)
{
  CWVCencSingleSampleDecrypter* decrypter = new CWVCencSingleSampleDecrypter(
      *m_WVCdmAdapter, pssh, defaultKeyId, skipSessionMessage, cryptoMode, this);
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
                                   const uint8_t* keyId,
                                   uint32_t media,
                                   IDecrypter::DecrypterCapabilites& caps)
{
  if (!decrypter)
  {
    caps = {0, 0, 0};
    return;
  }

  static_cast<CWVCencSingleSampleDecrypter*>(decrypter)->GetCapabilities(keyId, media, caps);
}

bool CWVDecrypter::HasLicenseKey(Adaptive_CencSingleSampleDecrypter* decrypter,
                                 const uint8_t* keyId)
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

void CWVDecrypter::SetLibraryPath(const char* libraryPath)
{
  m_strLibraryPath = libraryPath;

  const char* pathSep{libraryPath[0] && libraryPath[1] == ':' && isalpha(libraryPath[0]) ? "\\"
                                                                                         : "/"};

  if (m_strLibraryPath.size() && m_strLibraryPath.back() != pathSep[0])
    m_strLibraryPath += pathSep;
}

void CWVDecrypter::SetProfilePath(const std::string& profilePath)
{
  m_strProfilePath = profilePath;

  const char* pathSep{profilePath[0] && profilePath[1] == ':' && isalpha(profilePath[0]) ? "\\"
                                                                                         : "/"};

  if (m_strProfilePath.size() && m_strProfilePath.back() != pathSep[0])
    m_strProfilePath += pathSep;

  //let us make cdm userdata out of the addonpath and share them between addons
  m_strProfilePath.resize(m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 2));
  m_strProfilePath.resize(m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 1));
  m_strProfilePath.resize(m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 1) +
                          1);

  kodi::vfs::CreateDirectory(m_strProfilePath.c_str());
  m_strProfilePath += "cdm";
  m_strProfilePath += pathSep;
  kodi::vfs::CreateDirectory(m_strProfilePath.c_str());
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
