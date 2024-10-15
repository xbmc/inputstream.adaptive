/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WVCencSingleSampleDecrypter.h"

#include "CompSettings.h"
#include "SrvBroker.h"
#include "decrypters/HelperWv.h"
#include "decrypters/Helpers.h"
#include "utils/Base64Utils.h"
#include "utils/CurlUtils.h"
#include "utils/DigestMD5Utils.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

#include <chrono>
#include <thread>

#include <jni/src/MediaDrm.h>

using namespace UTILS;

CWVCencSingleSampleDecrypterA::CWVCencSingleSampleDecrypterA(
    IWVCdmAdapter<jni::CJNIMediaDrm>* cdmAdapter,
    const std::vector<uint8_t>& pssh,
    const std::vector<uint8_t>& defaultKeyId)
  : m_cdmAdapter(cdmAdapter),
    m_pssh(pssh),
    m_isProvisioningRequested(false),
    m_isKeyUpdateRequested(false),
    m_hdcpLimit(0),
    m_resolutionLimit(0),
    m_defaultKeyId{defaultKeyId}
{
  SetParentIsOwner(false);

  if (pssh.size() < 4 || pssh.size() > 65535)
  {
    LOG::LogF(LOGERROR, "PSSH init data with length %zu seems not to be cenc init data",
              pssh.size());
    return;
  }

  m_cdmAdapter->AttachObserver(this);

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string fileName =
        STRING::ToUpper(DRM::KeySystemToUUIDstr(m_cdmAdapter->GetKeySystem())) + ".init";
    std::string debugFilePath = FILESYS::PathCombine(m_cdmAdapter->GetLibraryPath(), fileName);
    FILESYS::SaveFile(debugFilePath, {m_pssh.cbegin(), m_pssh.cend()}, true);
  }

  m_initialPssh = m_pssh;

  if (m_cdmAdapter->GetKeySystem() == DRM::KS_PLAYREADY)
  {
    for (auto& [keyName, keyValue] : m_cdmAdapter->GetConfig().optKeyReqParams)
    {
      if (keyName == "custom_data")
        m_optParams["PRCustomData"] = keyValue;
    }
  }

  /*
  std::vector<char> pui = m_cdmAdapter->GetCDM()->getPropertyByteArray("provisioningUniqueId");
  xbmc_jnienv()->ExceptionClear();
  if (pui.size() > 0)
  {
    std::string encoded = BASE64::Encode(pui);
    m_optParams["CDMID"] = encoded;
  }
  */

  bool L3FallbackRequested = false;
RETRY_OPEN:
  m_sessionIdVec = m_cdmAdapter->GetCDM()->openSession();
  m_sessionId.assign(m_sessionIdVec.cbegin(), m_sessionIdVec.cend());
  if (xbmc_jnienv()->ExceptionCheck())
  {
    xbmc_jnienv()->ExceptionClear();
    if (!m_isProvisioningRequested)
    {
      LOG::LogF(LOGWARNING, "Exception during open session - provisioning...");
      m_isProvisioningRequested = true;
      if (!ProvisionRequest())
      {
        if (!L3FallbackRequested &&
            m_cdmAdapter->GetCDM()->getPropertyString("securityLevel") == "L1")
        {
          LOG::LogF(LOGWARNING, "L1 provisioning failed - retrying with L3...");
          L3FallbackRequested = true;
          m_isProvisioningRequested = false;
          m_cdmAdapter->GetCDM()->setPropertyString("securityLevel", "L3");
          goto RETRY_OPEN;
        }
        else
          return;
      }
      goto RETRY_OPEN;
    }
    else
    {
      LOG::LogF(LOGERROR, "Exception during open session - abort");
      return;
    }
  }

  if (m_sessionId.empty())
  {
    LOG::LogF(LOGERROR, "Unable to open DRM session");
    return;
  }

  if (m_cdmAdapter->GetKeySystem() != DRM::KS_PLAYREADY)
  {
    int maxSecuritylevel = m_cdmAdapter->GetCDM()->getMaxSecurityLevel();
    xbmc_jnienv()->ExceptionClear();

    LOG::Log(LOGDEBUG, "Session ID: %s, Max security level: %d", m_sessionId.c_str(), maxSecuritylevel);
  }
}

CWVCencSingleSampleDecrypterA::~CWVCencSingleSampleDecrypterA()
{
  // This decrypter can be used/shared with more streams "sessions"
  // since it is used wrapped in a shared_ptr the destructor will be called only
  // when the last stream "session" will be deleted, and so it can close the CDM session.
  if (!m_sessionId.empty())
  {
    /*
    m_cdmAdapter->GetCDM()->removeKeys(m_sessionIdVec);
    if (xbmc_jnienv()->ExceptionCheck())
    {
      LOG::LogF(LOGERROR, "removeKeys has raised an exception");
      xbmc_jnienv()->ExceptionClear();
    }
    */
    m_cdmAdapter->GetCDM()->closeSession(m_sessionIdVec);
    if (xbmc_jnienv()->ExceptionCheck())
    {
      LOG::LogF(LOGERROR, "closeSession has raised an exception");
      xbmc_jnienv()->ExceptionClear();
    }
    else
      LOG::LogF(LOGDEBUG, "MediaDrm Session ID %s closed", m_sessionId.c_str());

    m_sessionIdVec.clear();
    m_sessionId.clear();
  }

  m_cdmAdapter->DetachObserver(this);
}

std::string CWVCencSingleSampleDecrypterA::GetSessionId()
{
  return m_sessionId;
}

std::vector<uint8_t> CWVCencSingleSampleDecrypterA::GetChallengeData()
{
  return m_keyRequestData;
}

bool CWVCencSingleSampleDecrypterA::HasLicenseKey(const std::vector<uint8_t>& keyId)
{
  // true = one session for all streams, false = one sessions per stream
  // false fixes pixaltion issues on some devices when manifest has multiple encrypted streams
  return true;
}

void CWVCencSingleSampleDecrypterA::GetCapabilities(const std::vector<uint8_t>& keyId,
                                                    uint32_t media,
                                                    DRM::DecrypterCapabilites& caps)
{
  caps = {DRM::DecrypterCapabilites::SSD_SECURE_PATH |
              DRM::DecrypterCapabilites::SSD_ANNEXB_REQUIRED,
          0, m_hdcpLimit};

  if (caps.hdcpLimit == 0)
    caps.hdcpLimit = m_resolutionLimit;

  // Note: Currently we check for L1 only, Kodi core at later time check if secure decoder is needed
  // by using requiresSecureDecoderComponent method of MediaDrm API
  // https://github.com/xbmc/xbmc/blob/Nexus/xbmc/cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodecAndroidMediaCodec.cpp#L639-L641
  if (m_cdmAdapter->GetCDM()->getPropertyString("securityLevel") == "L1")
  {
    caps.hdcpLimit = m_resolutionLimit; //No restriction
    caps.flags |= DRM::DecrypterCapabilites::SSD_SECURE_DECODER;
  }
  LOG::LogF(LOGDEBUG, "hdcpLimit: %i", caps.hdcpLimit);

  caps.hdcpVersion = 99;
}

void CWVCencSingleSampleDecrypterA::OnNotify(const CdmMessage& message)
{
  if (!m_sessionId.empty() && m_sessionId != message.sessionId)
    return;

  if (message.type == CdmMessageType::EVENT_KEY_REQUIRED)
  {
    RequestNewKeys();
  }
}

bool CWVCencSingleSampleDecrypterA::ProvisionRequest()
{
  LOG::Log(LOGWARNING, "Provision data request (MediaDrm instance: %p)", m_cdmAdapter->GetCDM().get());

  jni::CJNIMediaDrmProvisionRequest request = m_cdmAdapter->GetCDM()->getProvisionRequest();
  if (xbmc_jnienv()->ExceptionCheck())
  {
    LOG::LogF(LOGERROR, "getProvisionRequest has raised an exception");
    xbmc_jnienv()->ExceptionClear();
    return false;
  }

  std::vector<uint8_t> provData = request.getData();
  std::string url = request.getDefaultUrl();

  LOG::Log(LOGDEBUG, "Provision data size: %lu, url: %s", provData.size(), url.c_str());

  std::string reqData("{\"signedRequest\":\"");
  reqData += std::string(provData.cbegin(), provData.cend());
  reqData += "\"}";
  reqData = BASE64::Encode(reqData);

  CURL::CUrl file(url);
  file.AddHeader("Content-Type", "application/json");
  file.AddHeader("postdata", reqData.c_str());

  int statusCode = file.Open();
  if (statusCode == -1 || statusCode >= 400)
  {
    LOG::Log(LOGERROR, "Provisioning server returned failure");
    return false;
  }
  provData.clear();

  // read the file
  std::string response;
  CURL::ReadStatus downloadStatus = CURL::ReadStatus::CHUNK_READ;
  while (downloadStatus == CURL::ReadStatus::CHUNK_READ)
  {
    downloadStatus = file.Read(response);
  }
  std::copy(response.begin(), response.end(), std::back_inserter(provData));

  m_cdmAdapter->GetCDM()->provideProvisionResponse(provData);
  if (xbmc_jnienv()->ExceptionCheck())
  {
    LOG::LogF(LOGERROR, "provideProvisionResponse has raised an exception");
    xbmc_jnienv()->ExceptionClear();
    return false;
  }
  return true;
}

bool CWVCencSingleSampleDecrypterA::GetKeyRequest(std::vector<uint8_t>& keyRequestData)
{
  jni::CJNIMediaDrmKeyRequest keyRequest = m_cdmAdapter->GetCDM()->getKeyRequest(
      m_sessionIdVec, m_pssh, "video/mp4", jni::CJNIMediaDrm::KEY_TYPE_STREAMING, m_optParams);

  if (xbmc_jnienv()->ExceptionCheck())
  {
    xbmc_jnienv()->ExceptionClear();
    if (!m_isProvisioningRequested)
    {
      LOG::Log(LOGWARNING, "Key request not successful - trying provisioning");
      m_isProvisioningRequested = true;
      return GetKeyRequest(keyRequestData);
    }
    else
      LOG::LogF(LOGERROR, "Key request not successful");
    return false;
  }

  keyRequestData = keyRequest.getData();
  LOG::Log(LOGDEBUG, "Key request successful size: %lu", keyRequestData.size());
  return true;
}

bool CWVCencSingleSampleDecrypterA::KeyUpdateRequest(bool waitKeys, bool skipSessionMessage)
{
  if (!GetKeyRequest(m_keyRequestData))
    return false;

  m_pssh.clear();
  m_optParams.clear();

  if (skipSessionMessage)
    return true;

  //! @todo: exists ways to wait callbacks without make uses of wait loop and thread sleep
  m_isKeyUpdateRequested = false;
  if (!SendSessionMessage(m_keyRequestData))
    return false;

  if (waitKeys && m_keyRequestData.size() == 2) // Service Certificate call
  {
    for (unsigned int i(0); i < 100 && !m_isKeyUpdateRequested; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

    if (m_isKeyUpdateRequested)
      KeyUpdateRequest(false, false);
    else
    {
      LOG::LogF(LOGERROR, "Timeout waiting for EVENT_KEYS_REQUIRED!");
      return false;
    }
  }

  if (m_cdmAdapter->GetKeySystem() != DRM::KS_PLAYREADY)
  {
    int securityLevel = m_cdmAdapter->GetCDM()->getSecurityLevel(m_sessionIdVec);
    xbmc_jnienv()->ExceptionClear();
    LOG::Log(LOGDEBUG, "Security level: %d", securityLevel);

    std::map<std::string, std::string> keyStatus =
        m_cdmAdapter->GetCDM()->queryKeyStatus(m_sessionIdVec);
    LOG::Log(LOGDEBUG, "Key status (%ld):", keyStatus.size());
    for (auto const& ks : keyStatus)
    {
      LOG::Log(LOGDEBUG, "-> %s -> %s", ks.first.c_str(), ks.second.c_str());
    }
  }
  return true;
}

bool CWVCencSingleSampleDecrypterA::SendSessionMessage(const std::vector<uint8_t>& challenge)
{
  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string fileName =
        STRING::ToUpper(DRM::KeySystemToUUIDstr(m_cdmAdapter->GetKeySystem())) + ".challenge";
    std::string debugFilePath = FILESYS::PathCombine(m_cdmAdapter->GetLibraryPath(), fileName);
    UTILS::FILESYS::SaveFile(debugFilePath, {challenge.cbegin(), challenge.cend()}, true);
  }

  const DRM::Config drmCfg = m_cdmAdapter->GetConfig();
  const DRM::Config::License& licConfig = drmCfg.license;
  std::string reqData;

  if (!licConfig.isHttpGetRequest) // Make HTTP POST request
  {
    if (licConfig.reqData.empty()) // By default add raw challenge
    {
      reqData.assign(challenge.cbegin(), challenge.cend());
    }
    else
    {
      if (BASE64::IsValidBase64(licConfig.reqData))
        reqData = BASE64::DecodeToStr(licConfig.reqData);
      else //! @todo: this fallback as plain text must be removed when the deprecated DRM properties are removed, and so replace it to return error
        reqData = licConfig.reqData;

      // Some services have a customized license server that require data to be wrapped with their formats (e.g. JSON).
      // Here we provide a built-in way to customize the license data to be sent, this avoid force add-ons to integrate
      // an HTTP server proxy to manage the license data request/response, and so use Kodi properties to set wrappers.
      if (m_cdmAdapter->GetKeySystem() == DRM::KS_WIDEVINE &&
          !DRM::WvWrapLicense(reqData, challenge, m_sessionId, m_defaultKeyId, m_pssh,
                              licConfig.wrapper, drmCfg.isNewConfig))
      {
        return false;
      }
    }
  }

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string fileName =
        STRING::ToUpper(DRM::KeySystemToUUIDstr(m_cdmAdapter->GetKeySystem())) + ".request";
    std::string debugFilePath = FILESYS::PathCombine(m_cdmAdapter->GetLibraryPath(), fileName);
    UTILS::FILESYS::SaveFile(debugFilePath, reqData, true);
  }

  std::string url = licConfig.serverUrl;
  DRM::TranslateLicenseUrlPh(url, challenge, drmCfg.isNewConfig);

  CURL::CUrl cUrl{url, reqData};
  cUrl.AddHeaders(licConfig.reqHeaders);

  const int statusCode = cUrl.Open();

  if (statusCode == -1 || statusCode >= 400)
  {
    LOG::Log(LOGERROR, "License server returned failure (HTTP error %i)", statusCode);
    return false;
  }

  std::string respData;
  if (cUrl.Read(respData) == CURL::ReadStatus::ERROR)
  {
    LOG::LogF(LOGERROR, "Cannot read license server response");
    return false;
  }

  const std::string resLimit = cUrl.GetResponseHeader("X-Limit-Video"); // Custom header
  const std::string respContentType = cUrl.GetResponseHeader("Content-Type");

  if (!resLimit.empty())
  {
    // To force limit playable streams resolutions
    size_t posMax = resLimit.find("max=");
    if (posMax != std::string::npos)
      m_resolutionLimit = std::atoi(resLimit.data() + (posMax + 4));
  }

  // The first request could be the license certificate request
  // this request is done by sending a challenge of 2 bytes, 0x08 0x04 (CAQ=)
  const bool isCertRequest = challenge.size() == 2 && challenge[0] == 0x08 && challenge[1] == 0x04;
  LOG::LogF(LOGDEBUG, "IS CERT REQ? %i", isCertRequest);

  int hdcpLimit{0};

  if (!isCertRequest)
  {
    // Unwrap license response
    if (m_cdmAdapter->GetKeySystem() == DRM::KS_WIDEVINE)
    {
      std::string unwrappedData;
      // Some services have a customized license server that require data to be wrapped with their formats (e.g. JSON).
      // Here we provide a built-in way to unwrap the license data received, this avoid force add-ons to integrate
      // a HTTP server proxy to manage the license data request/response, and so use Kodi properties to set wrappers.
      if (!DRM::WvUnwrapLicense(licConfig.unwrapper, licConfig.unwrapperParams, respContentType,
                                respData, unwrappedData, hdcpLimit))
      {
        return false;
      }
      respData = unwrappedData;
    }

    if (m_cdmAdapter->GetKeySystem() == DRM::KS_PLAYREADY &&
        respData.find("<LicenseNonce>") == std::string::npos)
    {
      size_t dstPos = respData.find("</Licenses>");
      std::string challengeStr(challenge.cbegin(), challenge.cend());
      size_t srcPosS = challengeStr.find("<LicenseNonce>");
      if (dstPos != std::string::npos && srcPosS != std::string::npos)
      {
        LOG::Log(LOGDEBUG, "Injecting missing PlayReady <LicenseNonce> tag to license response");
        size_t srcPosE = challengeStr.find("</LicenseNonce>", srcPosS);
        if (srcPosE != std::string::npos)
          respData.insert(dstPos + 11, challengeStr.c_str() + srcPosS, srcPosE - srcPosS + 15);
      }
    }
  }

  if (isCertRequest && CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string fileName =
        STRING::ToUpper(DRM::KeySystemToUUIDstr(m_cdmAdapter->GetKeySystem())) + ".response.cert";
    std::string debugFilePath = FILESYS::PathCombine(m_cdmAdapter->GetLibraryPath(), fileName);
    FILESYS::SaveFile(debugFilePath, respData, true);
  }
  if (!isCertRequest && CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string fileName =
        STRING::ToUpper(DRM::KeySystemToUUIDstr(m_cdmAdapter->GetKeySystem())) + ".response";
    std::string debugFilePath = FILESYS::PathCombine(m_cdmAdapter->GetLibraryPath(), fileName);
    FILESYS::SaveFile(debugFilePath, respData, true);
  }

  m_keySetId = m_cdmAdapter->GetCDM()->provideKeyResponse(
      m_sessionIdVec, std::vector<char>(respData.data(), respData.data() + respData.size()));
  if (xbmc_jnienv()->ExceptionCheck())
  {
    LOG::LogF(LOGERROR, "MediaDrm: provideKeyResponse has raised an exception");
    xbmc_jnienv()->ExceptionClear();
    return false;
  }

  if (isCertRequest)
    m_cdmAdapter->SaveServiceCertificate();

  LOG::Log(LOGDEBUG, "License update successful");
  return true;
}

AP4_Result CWVCencSingleSampleDecrypterA::SetFragmentInfo(AP4_UI32 poolId,
                                                          const std::vector<uint8_t>& keyId,
                                                          const AP4_UI08 nalLengthSize,
                                                          AP4_DataBuffer& annexbSpsPps,
                                                          AP4_UI32 flags,
                                                          CryptoInfo cryptoInfo)
{
  if (poolId >= m_fragmentPool.size())
    return AP4_ERROR_OUT_OF_RANGE;

  m_fragmentPool[poolId].m_key = keyId;
  m_fragmentPool[poolId].m_nalLengthSize = nalLengthSize;
  m_fragmentPool[poolId].m_annexbSpsPps.SetData(annexbSpsPps.GetData(), annexbSpsPps.GetDataSize());
  m_fragmentPool[poolId].m_decrypterFlags = flags;

  if (m_isKeyUpdateRequested)
    KeyUpdateRequest(false, false);

  return AP4_SUCCESS;
}

AP4_UI32 CWVCencSingleSampleDecrypterA::AddPool()
{
  for (size_t i(0); i < m_fragmentPool.size(); ++i)
    if (m_fragmentPool[i].m_nalLengthSize == 99)
    {
      m_fragmentPool[i].m_nalLengthSize = 0;
      return i;
    }
  m_fragmentPool.push_back(FINFO());
  m_fragmentPool.back().m_nalLengthSize = 0;
  return static_cast<AP4_UI32>(m_fragmentPool.size() - 1);
}


void CWVCencSingleSampleDecrypterA::RemovePool(AP4_UI32 poolId)
{
  m_fragmentPool[poolId].m_nalLengthSize = 99;
  m_fragmentPool[poolId].m_key.clear();
}

AP4_Result CWVCencSingleSampleDecrypterA::DecryptSampleData(AP4_UI32 poolId,
                                                           AP4_DataBuffer& dataIn,
                                                           AP4_DataBuffer& dataOut,
                                                           const AP4_UI08* iv,
                                                           unsigned int subsampleCount,
                                                           const AP4_UI16* bytesOfCleartextData,
                                                           const AP4_UI32* bytesOfEncryptedData)
{
  if (!m_cdmAdapter->GetCDM())
    return AP4_ERROR_INVALID_STATE;

  if (dataIn.GetDataSize() > 0)
  {
    FINFO& fragInfo(m_fragmentPool[poolId]);

    if (fragInfo.m_nalLengthSize > 4)
    {
      LOG::LogF(LOGERROR, "Nalu length size > 4 not supported");
      return AP4_ERROR_NOT_SUPPORTED;
    }

    AP4_UI16 dummyClear = 0;
    AP4_UI32 dummyCipher(dataIn.GetDataSize());

    if (iv)
    {
      if (!subsampleCount)
      {
        subsampleCount = 1;
        bytesOfCleartextData = &dummyClear;
        bytesOfEncryptedData = &dummyCipher;
      }

      dataOut.SetData(reinterpret_cast<const AP4_Byte*>(&subsampleCount), sizeof(subsampleCount));
      dataOut.AppendData(reinterpret_cast<const AP4_Byte*>(bytesOfCleartextData),
                         subsampleCount * sizeof(AP4_UI16));
      dataOut.AppendData(reinterpret_cast<const AP4_Byte*>(bytesOfEncryptedData),
                         subsampleCount * sizeof(AP4_UI32));
      dataOut.AppendData(reinterpret_cast<const AP4_Byte*>(iv), 16);
      dataOut.AppendData(fragInfo.m_key.data(), static_cast<AP4_Size>(fragInfo.m_key.size()));
    }
    else
    {
      dataOut.SetDataSize(0);
      bytesOfCleartextData = &dummyClear;
      bytesOfEncryptedData = &dummyCipher;
    }

    if (fragInfo.m_nalLengthSize && (!iv || bytesOfCleartextData[0] > 0))
    {
      //check NAL / subsample
      const AP4_Byte* packetIn(dataIn.GetData());
      const AP4_Byte* packetInEnd(dataIn.GetData() + dataIn.GetDataSize());
      // Byte position of "bytesOfCleartextData" where to set the size of the data,
      // by default starts after the subsample count data (so the size of subsampleCount data type)
      size_t clrDataBytePos = sizeof(subsampleCount);
      // size_t nalUnitCount = 0; //! @todo: what is the point of this?
      size_t nalUnitSum = 0;
      // size_t configSize = 0; //! @todo:what is the point of this?

      while (packetIn < packetInEnd)
      {
        uint32_t nalsize = 0;
        for (size_t i = 0; i < fragInfo.m_nalLengthSize; ++i)
        {
          nalsize = (nalsize << 8) + *packetIn++;
        };

        //look if we have to inject sps / pps
        if (fragInfo.m_annexbSpsPps.GetDataSize() && (*packetIn & 0x1F) != 9 /*AVC_NAL_AUD*/)
        {
          dataOut.AppendData(fragInfo.m_annexbSpsPps.GetData(),
                             fragInfo.m_annexbSpsPps.GetDataSize());
          if (iv)
          {
            // Update the byte containing the data size of current subsample referred to clear bytes array
            AP4_UI16* clrb_out = reinterpret_cast<AP4_UI16*>(dataOut.UseData() + clrDataBytePos);
            *clrb_out += fragInfo.m_annexbSpsPps.GetDataSize();
          }
          // configSize = fragInfo.m_annexbSpsPps.GetDataSize();
          fragInfo.m_annexbSpsPps.SetDataSize(0);
        }

        //Annex-B Start pos
        static AP4_Byte annexbStartCode[4] = {0x00, 0x00, 0x00, 0x01};
        dataOut.AppendData(annexbStartCode, 4);
        dataOut.AppendData(packetIn, nalsize);
        packetIn += nalsize;

        if (iv)
        {
          // Update the byte containing the data size of current subsample referred to clear bytes array
          AP4_UI16* clrb_out = reinterpret_cast<AP4_UI16*>(dataOut.UseData() + clrDataBytePos);
          *clrb_out += (4 - fragInfo.m_nalLengthSize);
        }
        
        // ++nalUnitCount;

        if (!iv)
        {
          nalUnitSum = 0;
        }
        else if (nalsize + fragInfo.m_nalLengthSize + nalUnitSum >=
                 *bytesOfCleartextData + *bytesOfEncryptedData)
        {
          AP4_UI32 summedBytes = 0;
          do
          {
            summedBytes += *bytesOfCleartextData + *bytesOfEncryptedData;
            ++bytesOfCleartextData;
            ++bytesOfEncryptedData;
            clrDataBytePos += sizeof(AP4_UI16); // Move to the next clear data subsample byte position
            --subsampleCount;
          } while (subsampleCount && nalsize + fragInfo.m_nalLengthSize + nalUnitSum > summedBytes);

          if (nalsize + fragInfo.m_nalLengthSize + nalUnitSum > summedBytes)
          {
            LOG::LogF(LOGERROR, "NAL Unit exceeds subsample definition (nls: %u) %u -> %u ",
                      static_cast<unsigned int>(fragInfo.m_nalLengthSize),
                      static_cast<unsigned int>(nalsize + fragInfo.m_nalLengthSize + nalUnitSum),
                      summedBytes);
            return AP4_ERROR_NOT_SUPPORTED;
          }
          nalUnitSum = 0;
        }
        else
          nalUnitSum += nalsize + fragInfo.m_nalLengthSize;
      }
      if (packetIn != packetInEnd || subsampleCount)
      {
        LOG::LogF(LOGERROR, "NAL Unit definition incomplete (nls: %d) %d -> %u ",
                  fragInfo.m_nalLengthSize, (int)(packetInEnd - packetIn), subsampleCount);
        return AP4_ERROR_NOT_SUPPORTED;
      }
    }
    else
    {
      dataOut.AppendData(dataIn.GetData(), dataIn.GetDataSize());
      fragInfo.m_annexbSpsPps.SetDataSize(0);
    }
  }
  else
    dataOut.SetDataSize(0);
  return AP4_SUCCESS;
}
