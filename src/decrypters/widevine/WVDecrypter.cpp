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
#include "utils/GUIUtils.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

using namespace DRM;
using namespace UTILS;

CWVDecrypter::~CWVDecrypter()
{
  m_WVCdmAdapter.reset();
}

bool CWVDecrypter::IsKeySystemSupported(std::string_view keySystem)
{
  return keySystem == KS_WIDEVINE;
}

std::shared_ptr<Adaptive_CencSingleSampleDecrypter> CWVDecrypter::CreateSingleSampleDecrypter(
    const DRM::Config& config,
    const std::vector<uint8_t>& defaultKeyId,
    CryptoMode cryptoMode)
{
  if (!m_WVCdmAdapter)
  {
    auto cdmAdapter = std::make_shared<CWVCdmAdapter>();
    const SResult ret = cdmAdapter->Initialize(config, this);

    if (ret.IsFailed())
      return nullptr;

    m_WVCdmAdapter = cdmAdapter;
  }
  
  return std::make_shared<CWVCencSingleSampleDecrypter>(m_WVCdmAdapter.get(), defaultKeyId,
                                                        cryptoMode);
}

void CWVDecrypter::GetCapabilities(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                   const std::vector<uint8_t>& keyId,
                                   DRM::Capabilities& caps,
                                   DRMMediaType mediaType)
{
  if (!decrypter)
  {
    caps = {0, 0, 0};
    return;
  }

  auto wvDecrypter = std::dynamic_pointer_cast<CWVCencSingleSampleDecrypter>(decrypter);
  if (wvDecrypter)
  {
    wvDecrypter->GetCapabilities(keyId, caps, mediaType);
  }
  else
    LOG::LogF(LOGFATAL, "Cannot cast the decrypter shared pointer.");
}

std::optional<bool> CWVDecrypter::HasLicenseKey(
    std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
    const std::vector<uint8_t>& keyId)
{
  auto wvDecrypter = std::dynamic_pointer_cast<CWVCencSingleSampleDecrypter>(decrypter);
  if (wvDecrypter)
  {
    return wvDecrypter->HasKeyId(keyId);
  }
  else
    LOG::LogF(LOGFATAL, "Cannot cast the decrypter shared pointer.");

  return std::nullopt;
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
    m_decodingDecrypter->ResetVideo();
}

void CWVDecrypter::DisposeDecoder()
{
  m_decodingDecrypter = nullptr;
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
