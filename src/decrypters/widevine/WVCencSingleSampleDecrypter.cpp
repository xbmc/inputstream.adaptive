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
#include "CdmDecryptedBlock.h"
#include "CdmFixedBuffer.h"
#include "CdmTypeConversion.h"
#include "WVCdmAdapter.h"
#include "cdm/media/cdm/cdm_adapter.h"
#include "decrypters/Helpers.h"
#include "utils/Base64Utils.h"
#include "utils/CurlUtils.h"
#include "utils/DigestMD5Utils.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <mutex>
#include <thread>

using namespace UTILS;

void CWVCencSingleSampleDecrypter::SetSession(const std::string sessionId,
                                              const uint8_t* data,
                                              const size_t dataSize)
{
  std::lock_guard<std::mutex> lock(m_renewalLock);

  m_strSession = sessionId;
  m_challenge.SetData(data, dataSize);
  LOG::LogF(LOGDEBUG, "Opened widevine session ID: %s", m_strSession.c_str());
}

CWVCencSingleSampleDecrypter::CWVCencSingleSampleDecrypter(
    IWVCdmAdapter<media::CdmAdapter>* cdmAdapter,
    const std::vector<uint8_t>& pssh,
    const std::vector<uint8_t>& defaultKeyId,
    bool skipSessionMessage,
    CryptoMode cryptoMode)
  : m_cdmAdapter(cdmAdapter),
    m_pssh(pssh),
    m_hdcpVersion(99),
    m_hdcpLimit(0),
    m_resolutionLimit(0),
    m_promiseId(1),
    m_isDrained(true),
    m_defaultKeyId(defaultKeyId),
    m_EncryptionMode(cryptoMode)
{
  SetParentIsOwner(false);

  if (pssh.size() < 4 || pssh.size() > 4096)
  {
    LOG::LogF(LOGERROR, "PSSH init data with length %zu seems not to be cenc init data",
              pssh.size());
    return;
  }

  m_cdmAdapter->AttachObserver(this);

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(m_cdmAdapter->GetLibraryPath(),
                                                     "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.init");
    UTILS::FILESYS::SaveFile(debugFilePath, {m_pssh.cbegin(), m_pssh.cend()}, true);
  }

  m_cdmAdapter->GetCDM()->CreateSessionAndGenerateRequest(
      m_promiseId++, cdm::SessionType::kTemporary, cdm::InitDataType::kCenc, m_pssh.data(),
      static_cast<uint32_t>(m_pssh.size()));

  //! @todo: loop with thread sleep should be removed, use callbacks
  int retrycount = 0;
  while (m_strSession.empty() && ++retrycount < 100)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

  if (m_strSession.empty())
  {
    LOG::LogF(LOGERROR, "Cannot perform License update, no session available");
    return;
  }

  if (skipSessionMessage)
    return;

  //! @todo: this loop is not so clear
  while (m_challenge.GetDataSize() > 0 && SendSessionMessage())
    ;
}

CWVCencSingleSampleDecrypter::~CWVCencSingleSampleDecrypter()
{
  // This decrypter can be used/shared with more streams "sessions"
  // since it is used wrapped in a shared_ptr the destructor will be called only
  // when the last stream "session" will be deleted, and so it can close the CDM session.
  CloseSessionId();

  m_cdmAdapter->DetachObserver(this);
}

void CWVCencSingleSampleDecrypter::GetCapabilities(const std::vector<uint8_t>& keyId,
                                                   uint32_t media,
                                                   DecrypterCapabilites& caps)
{
  caps = {0, m_hdcpVersion, m_hdcpLimit};

  if (m_strSession.empty())
  {
    LOG::LogF(LOGDEBUG, "Session empty");
    return;
  }

  caps.flags = DecrypterCapabilites::SSD_SUPPORTS_DECODING;

  if (m_keys.empty())
  {
    LOG::LogF(LOGDEBUG, "Keys empty");
    return;
  }

  if (!caps.hdcpLimit)
    caps.hdcpLimit = m_resolutionLimit;

  /*if (media == DecrypterCapabilites::SSD_MEDIA_VIDEO)
    caps.flags |= (DecrypterCapabilites::SSD_SECURE_PATH | DecrypterCapabilites::SSD_ANNEXB_REQUIRED);
  caps.flags |= DecrypterCapabilites::SSD_SINGLE_DECRYPT;
  return;*/

  //caps.flags |= (DecrypterCapabilites::SSD_SECURE_PATH | DecrypterCapabilites::SSD_ANNEXB_REQUIRED);
  //return;

  /*for (auto k : m_keys)
    if (!key || memcmp(k.m_keyId.data(), key, 16) == 0)
    {
      if (k.status != 0)
      {
        if (media == DecrypterCapabilites::SSD_MEDIA_VIDEO)
          caps.flags |= (DecrypterCapabilites::SSD_SECURE_PATH | DecrypterCapabilites::SSD_ANNEXB_REQUIRED);
        else
          caps.flags = DecrypterCapabilites::SSD_INVALID;
      }
      break;
    }
    */
  if ((caps.flags & DecrypterCapabilites::SSD_SUPPORTS_DECODING) != 0)
  {
    AP4_UI32 poolId(AddPool());
    m_fragmentPool[poolId].m_key = keyId.empty() ? m_keys.front().m_keyId : keyId;
    m_fragmentPool[poolId].m_cryptoInfo.m_mode = m_EncryptionMode;

    AP4_DataBuffer in;
    AP4_DataBuffer out;
    AP4_UI32 encryptedBytes[2] = {1, 1};
    AP4_UI16 clearBytes[2] = {5, 5};
    AP4_Byte testData[12] = {0, 0, 0, 1, 9, 255, 0, 0, 0, 1, 10, 255};
    const AP4_UI08 iv[] = {1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0};
    in.SetBuffer(testData, 12);
    in.SetDataSize(12);
    try
    {
      encryptedBytes[0] = 12;
      clearBytes[0] = 0;
      if (DecryptSampleData(poolId, in, out, iv, 1, clearBytes, encryptedBytes) != AP4_SUCCESS)
      {
        LOG::LogF(LOGDEBUG, "Single decrypt failed, secure path only");
        if (media == DecrypterCapabilites::SSD_MEDIA_VIDEO)
          caps.flags |= (DecrypterCapabilites::SSD_SECURE_PATH |
                         DecrypterCapabilites::SSD_ANNEXB_REQUIRED);
        else
          caps.flags = DecrypterCapabilites::SSD_INVALID;
      }
      else
      {
        LOG::LogF(LOGDEBUG, "Single decrypt possible");
        caps.flags |= DecrypterCapabilites::SSD_SINGLE_DECRYPT;
        caps.hdcpVersion = 99;
        caps.hdcpLimit = m_resolutionLimit;
      }
    }
    catch (const std::exception& e)
    {
      LOG::LogF(LOGDEBUG, "Decrypt error, assuming secure path: %s", e.what());
      caps.flags |= (DecrypterCapabilites::SSD_SECURE_PATH |
                     DecrypterCapabilites::SSD_ANNEXB_REQUIRED);
    }
    RemovePool(poolId);
  }
  else
  {
    LOG::LogF(LOGDEBUG, "Decoding not supported");
  }
}

std::string CWVCencSingleSampleDecrypter::GetSessionId()
{
  return m_strSession;
}

void CWVCencSingleSampleDecrypter::CloseSessionId()
{
  if (!m_strSession.empty())
  {
    LOG::LogF(LOGDEBUG, "Closing widevine session ID: %s", m_strSession.c_str());
    m_cdmAdapter->GetCDM()->CloseSession(++m_promiseId, m_strSession.data(),
                                                 m_strSession.size());

    LOG::LogF(LOGDEBUG, "Widevine session ID %s closed", m_strSession.c_str());
    m_strSession.clear();
  }
}

AP4_DataBuffer CWVCencSingleSampleDecrypter::GetChallengeData()
{
  return m_challenge;
}

void CWVCencSingleSampleDecrypter::CheckLicenseRenewal()
{
  {
    std::lock_guard<std::mutex> lock(m_renewalLock);
    if (!m_challenge.GetDataSize())
      return;
  }
  SendSessionMessage();
}

bool CWVCencSingleSampleDecrypter::SendSessionMessage()
{
  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(
        m_cdmAdapter->GetLibraryPath(), "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.challenge");
    std::string data{reinterpret_cast<const char*>(m_challenge.GetData()),
                     m_challenge.GetDataSize()};
    UTILS::FILESYS::SaveFile(debugFilePath, data, true);
  }

  const DRM::Config drmCfg = m_cdmAdapter->GetConfig();
  const DRM::Config::License& licConfig = drmCfg.license;
  //! @todo: cleanup this var
  std::vector<uint8_t> challenge(reinterpret_cast<const uint8_t*>(m_challenge.GetData()),
                                 reinterpret_cast<const uint8_t*>(m_challenge.GetData()) +
                                     m_challenge.GetDataSize());
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
          !DRM::WvWrapLicense(reqData, challenge, m_strSession, m_defaultKeyId, m_pssh,
                              licConfig.wrapper, drmCfg.isNewConfig))
      {
        return false;
      }
    }
  }

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(
        m_cdmAdapter->GetLibraryPath(), "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.request");
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
  //! @todo: compare data not only by size but also by bytes
  const bool isCertRequest =
      m_challenge.GetDataSize() == 2 && respContentType == "application/octet-stream";
  m_challenge.SetDataSize(0);

  int hdcpLimit{0};

  // Unwrap license response
  if (!licConfig.unwrapper.empty() && m_cdmAdapter->GetKeySystem() == DRM::KS_WIDEVINE)
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

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(m_cdmAdapter->GetLibraryPath(),
                                                     "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED");
    if (isCertRequest)
      debugFilePath += ".cert.response";
    else
      debugFilePath += ".response";

    FILESYS::SaveFile(debugFilePath, respData, true);
  }

  m_cdmAdapter->GetCDM()->UpdateSession(++m_promiseId, m_strSession.data(), m_strSession.size(),
                                        reinterpret_cast<const uint8_t*>(respData.c_str()),
                                        respData.size());

  if (m_keys.empty())
  {
    LOG::LogF(LOGERROR, "License update not successful (no keys)");
    CloseSessionId();
    return false;
  }

  LOG::Log(LOGDEBUG, "License update successful");
  return true;
}

void CWVCencSingleSampleDecrypter::OnNotify(const CdmMessage& message)
{
  if (!m_strSession.empty() && m_strSession != message.sessionId)
    return;

  if (message.type == CdmMessageType::SESSION_MESSAGE)
  {
    SetSession(message.sessionId, message.data.data(), message.data.size());
  }
  else if (message.type == CdmMessageType::SESSION_KEY_CHANGE)
  {
    AddSessionKey(message.data.data(), message.data.size(), message.status);
  }
}

void CWVCencSingleSampleDecrypter::AddSessionKey(const uint8_t* data,
                                                 size_t dataSize,
                                                 uint32_t status)
{
  WVSKEY key;
  key.m_keyId.assign(data, data + dataSize);

  std::vector<WVSKEY>::iterator res;
  if ((res = std::find(m_keys.begin(), m_keys.end(), key)) == m_keys.end())
    res = m_keys.insert(res, key);
  res->status = static_cast<cdm::KeyStatus>(status);
}

bool CWVCencSingleSampleDecrypter::HasKeyId(const std::vector<uint8_t>& keyid)
{
  if (!keyid.empty())
  {
    for (const WVSKEY& key : m_keys)
    {
      if (key.m_keyId == keyid)
        return true;
    }
  }
  return false;
}

AP4_Result CWVCencSingleSampleDecrypter::SetFragmentInfo(AP4_UI32 poolId,
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
  m_fragmentPool[poolId].m_cryptoInfo = cryptoInfo;

  return AP4_SUCCESS;
}

AP4_UI32 CWVCencSingleSampleDecrypter::AddPool()
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


void CWVCencSingleSampleDecrypter::RemovePool(AP4_UI32 poolId)
{
  m_fragmentPool[poolId].m_nalLengthSize = 99;
  m_fragmentPool[poolId].m_key.clear();
}

void CWVCencSingleSampleDecrypter::LogDecryptError(const cdm::Status status,
                                                   const std::vector<uint8_t>& keyId)
{
  LOG::LogF(LOGDEBUG, "Decrypt failed with error code: %d and KID: %s", status,
            STRING::ToHexadecimal(keyId).c_str());
}

void CWVCencSingleSampleDecrypter::SetCdmSubsamples(std::vector<cdm::SubsampleEntry>& subsamples,
                                                    bool isCbc)
{
  if (isCbc)
  {
    subsamples.resize(1);
    subsamples[0] = {0, m_decryptIn.GetDataSize()};
  }
  else
  {
    subsamples.push_back({0, m_decryptIn.GetDataSize()});
  }
}

void CWVCencSingleSampleDecrypter::RepackSubsampleData(AP4_DataBuffer& dataIn,
                                                       AP4_DataBuffer& dataOut,
                                                       size_t& pos,
                                                       size_t& cipherPos,
                                                       const unsigned int subsamplePos,
                                                       const AP4_UI16* bytesOfCleartextData,
                                                       const AP4_UI32* bytesOfEncryptedData)
{
  dataOut.AppendData(dataIn.GetData() + pos, bytesOfCleartextData[subsamplePos]);
  pos += bytesOfCleartextData[subsamplePos];
  dataOut.AppendData(m_decryptOut.GetData() + cipherPos, bytesOfEncryptedData[subsamplePos]);
  pos += bytesOfEncryptedData[subsamplePos];
  cipherPos += bytesOfEncryptedData[subsamplePos];
}

void CWVCencSingleSampleDecrypter::UnpackSubsampleData(AP4_DataBuffer& dataIn,
                                                       size_t& pos,
                                                       const unsigned int subsamplePos,
                                                       const AP4_UI16* bytesOfCleartextData,
                                                       const AP4_UI32* bytesOfEncryptedData)
{
  pos += bytesOfCleartextData[subsamplePos];
  m_decryptIn.AppendData(dataIn.GetData() + pos, bytesOfEncryptedData[subsamplePos]);
  pos += bytesOfEncryptedData[subsamplePos];
}

void CWVCencSingleSampleDecrypter::SetInput(cdm::InputBuffer_2& cdmInputBuffer,
                                            const AP4_DataBuffer& inputData,
                                            const unsigned int subsampleCount,
                                            const uint8_t* iv,
                                            const FINFO& fragInfo,
                                            const std::vector<cdm::SubsampleEntry>& subsamples)
{
  cdmInputBuffer.data = inputData.GetData();
  cdmInputBuffer.data_size = inputData.GetDataSize();
  cdmInputBuffer.num_subsamples = subsampleCount;
  cdmInputBuffer.iv = iv;
  cdmInputBuffer.iv_size = 16; //Always 16, see AP4_CencSingleSampleDecrypter declaration.
  cdmInputBuffer.key_id = fragInfo.m_key.data();
  cdmInputBuffer.key_id_size = static_cast<uint32_t>(fragInfo.m_key.size());
  cdmInputBuffer.subsamples = subsamples.data();
  cdmInputBuffer.encryption_scheme = media::ToCdmEncryptionScheme(fragInfo.m_cryptoInfo.m_mode);
  cdmInputBuffer.timestamp = 0;
  cdmInputBuffer.pattern = {fragInfo.m_cryptoInfo.m_cryptBlocks,
                            fragInfo.m_cryptoInfo.m_skipBlocks};
}

/*----------------------------------------------------------------------
|   CWVCencSingleSampleDecrypter::DecryptSampleData
+---------------------------------------------------------------------*/
AP4_Result CWVCencSingleSampleDecrypter::DecryptSampleData(AP4_UI32 poolId,
                                                           AP4_DataBuffer& dataIn,
                                                           AP4_DataBuffer& dataOut,
                                                           const AP4_UI08* iv,
                                                           unsigned int subsampleCount,
                                                           const AP4_UI16* bytesOfCleartextData,
                                                           const AP4_UI32* bytesOfEncryptedData)
{
  if (!m_cdmAdapter->GetCDM())
  {
    dataOut.SetData(dataIn.GetData(), dataIn.GetDataSize());
    return AP4_SUCCESS;
  }

  FINFO& fragInfo(m_fragmentPool[poolId]);

  if (fragInfo.m_decrypterFlags &
      DecrypterCapabilites::SSD_SECURE_PATH) //we can not decrypt only
  {
    if (fragInfo.m_nalLengthSize > 4)
    {
      LOG::LogF(LOGERROR, "Nalu length size > 4 not supported");
      return AP4_ERROR_NOT_SUPPORTED;
    }

    AP4_UI16 dummyClear(0);
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
      const AP4_Byte *packetIn(dataIn.GetData()),
          *packetInEnd(dataIn.GetData() + dataIn.GetDataSize());
      // Byte position of "bytesOfCleartextData" where to set the size of the data,
      // by default starts after the subsample count data (so the size of subsampleCount data type)
      size_t clrDataBytePos = sizeof(subsampleCount);
      // size_t nalUnitCount = 0; // is there a use for this?
      size_t nalUnitSum = 0;
      // size_t configSize = 0; // is there a use for this?

      while (packetIn < packetInEnd)
      {
        uint32_t nalSize(0);
        for (size_t i = 0; i < fragInfo.m_nalLengthSize; ++i)
        {
          nalSize = (nalSize << 8) + *packetIn++;
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
        // Annex-B Start pos
        static AP4_Byte annexbStartCode[4] = {0x00, 0x00, 0x00, 0x01};
        dataOut.AppendData(annexbStartCode, 4);
        dataOut.AppendData(packetIn, nalSize);
        packetIn += nalSize;
        
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
        else if (nalSize + fragInfo.m_nalLengthSize + nalUnitSum >=
                 *bytesOfCleartextData + *bytesOfEncryptedData)
        {
          AP4_UI32 summedBytes(0);
          do
          {
            summedBytes += *bytesOfCleartextData + *bytesOfEncryptedData;
            ++bytesOfCleartextData;
            ++bytesOfEncryptedData;
            clrDataBytePos += sizeof(AP4_UI16); // Move to the next clear data subsample byte position
            --subsampleCount;
          } while (subsampleCount && nalSize + fragInfo.m_nalLengthSize + nalUnitSum > summedBytes);

          if (nalSize + fragInfo.m_nalLengthSize + nalUnitSum > summedBytes)
          {
            LOG::LogF(LOGERROR, "NAL Unit exceeds subsample definition (nls: %u) %u -> %u ",
                      static_cast<unsigned int>(fragInfo.m_nalLengthSize),
                      static_cast<unsigned int>(nalSize + fragInfo.m_nalLengthSize + nalUnitSum),
                      summedBytes);
            return AP4_ERROR_NOT_SUPPORTED;
          }
          nalUnitSum = 0;
        }
        else
          nalUnitSum += nalSize + fragInfo.m_nalLengthSize;
      }
      if (packetIn != packetInEnd || subsampleCount)
      {
        LOG::Log(LOGERROR, "NAL Unit definition incomplete (nls: %u) %u -> %u ",
                 static_cast<unsigned int>(fragInfo.m_nalLengthSize),
                 static_cast<unsigned int>(packetInEnd - packetIn), subsampleCount);
        return AP4_ERROR_NOT_SUPPORTED;
      }
    }
    else
      dataOut.AppendData(dataIn.GetData(), dataIn.GetDataSize());
    return AP4_SUCCESS;
  }

  if (fragInfo.m_key.empty())
  {
    LOG::LogF(LOGDEBUG, "No Key");
    return AP4_ERROR_INVALID_PARAMETERS;
  }

  dataOut.SetDataSize(0);

  uint16_t clearBytes = 0;
  uint32_t encryptedBytes = dataIn.GetDataSize();

  // check input parameters
  if (iv == NULL)
    return AP4_ERROR_INVALID_PARAMETERS;
  if (subsampleCount)
  {
    if (bytesOfCleartextData == NULL || bytesOfEncryptedData == NULL)
    {
      LOG::LogF(LOGDEBUG, "Invalid input params");
      return AP4_ERROR_INVALID_PARAMETERS;
    }
  }
  else
  {
    subsampleCount = 1;
    bytesOfCleartextData = &clearBytes;
    bytesOfEncryptedData = &encryptedBytes;
  }
  cdm::Status ret{cdm::Status::kSuccess};
  std::vector<cdm::SubsampleEntry> subsamples;
  subsamples.reserve(subsampleCount);

  bool useCbcDecrypt = fragInfo.m_cryptoInfo.m_mode == CryptoMode::AES_CBC;

  // We can only decrypt with subsamples set to 1
  // This must be handled differently for CENC and CBCS
  // CENC:
  // CDM should get 1 block of encrypted data per sample, encrypted data
  // from all subsamples should be formed into a contiguous block.
  // Even if there is only 1 subsample, we should remove cleartext data
  // from it before passing to CDM.
  // CBCS:
  // Due to the nature of this cipher subsamples must be decrypted separately

  const size_t iterations = useCbcDecrypt ? subsampleCount : 1;
  size_t absPos = 0;

  for (size_t i = 0; i < iterations; ++i)
  {
    m_decryptIn.Reserve(dataIn.GetDataSize());
    m_decryptIn.SetDataSize(0);
    size_t decryptInPos = absPos;
    if (useCbcDecrypt)
    {
      UnpackSubsampleData(dataIn, decryptInPos, i, bytesOfCleartextData, bytesOfEncryptedData);
    }
    else
    {
      for (size_t subsamplePos = 0; subsamplePos < subsampleCount; ++subsamplePos)
      {
        UnpackSubsampleData(dataIn, absPos, subsamplePos, bytesOfCleartextData,
                            bytesOfEncryptedData);
      }
    }

    if (m_decryptIn.GetDataSize() > 0) // remember to include when calling setcdmsubsamples
    {
      SetCdmSubsamples(subsamples, useCbcDecrypt);
    }

    else // we have nothing to decrypt in this iteration
    {
      if (useCbcDecrypt)
      {
        dataOut.AppendData(dataIn.GetData() + absPos, bytesOfCleartextData[i]);
        absPos += bytesOfCleartextData[i];
        continue;
      }
      else // we can exit here for CENC and just return the input buffer
      {
        dataOut.AppendData(dataIn.GetData(), dataIn.GetDataSize());
        return AP4_SUCCESS;
      }
    }

    cdm::InputBuffer_2 cdmIn;
    SetInput(cdmIn, m_decryptIn, 1, iv, fragInfo, subsamples);
    m_decryptOut.SetDataSize(m_decryptIn.GetDataSize());
    CdmBuffer buf = &m_decryptOut;
    CdmDecryptedBlock cdmOut;
    cdmOut.SetDecryptedBuffer(&buf);

    CheckLicenseRenewal();
    ret = m_cdmAdapter->GetCDM()->Decrypt(cdmIn, &cdmOut);

    if (ret == cdm::Status::kSuccess)
    {
      size_t cipherPos = 0;
      if (useCbcDecrypt)
      {
        RepackSubsampleData(dataIn, dataOut, absPos, cipherPos, i, bytesOfCleartextData,
                            bytesOfEncryptedData);
      }
      else
      {
        size_t absPos = 0;
        for (unsigned int i{0}; i < subsampleCount; ++i)
        {
          RepackSubsampleData(dataIn, dataOut, absPos, cipherPos, i, bytesOfCleartextData,
                              bytesOfEncryptedData);
        }
      }
    }
    else
    {
      LogDecryptError(ret, fragInfo.m_key);
    }
  }
  return (ret == cdm::Status::kSuccess) ? AP4_SUCCESS : AP4_ERROR_INVALID_PARAMETERS;
}

bool CWVCencSingleSampleDecrypter::OpenVideoDecoder(const VIDEOCODEC_INITDATA* initData)
{
  cdm::VideoDecoderConfig_3 vconfig = media::ToCdmVideoDecoderConfig(initData, m_EncryptionMode);

  // InputStream interface call OpenVideoDecoder also during playback when stream quality
  // change, so we reinitialize the decoder only when the codec change
  if (m_currentVideoDecConfig.has_value())
  {
    cdm::VideoDecoderConfig_3& currVidConfig = *m_currentVideoDecConfig;
    if (currVidConfig.codec == vconfig.codec && currVidConfig.profile == vconfig.profile)
      return true;

    m_cdmAdapter->GetCDM()->DeinitializeDecoder(cdm::StreamType::kStreamTypeVideo);
  }

  m_currentVideoDecConfig = vconfig;

  cdm::Status ret = m_cdmAdapter->GetCDM()->InitializeVideoDecoder(vconfig);
  m_videoFrames.clear();
  m_isDrained = true;

  LOG::LogF(LOGDEBUG, "Initialization returned status: %s", media::CdmStatusToString(ret).c_str());
  return ret == cdm::Status::kSuccess;
}

VIDEOCODEC_RETVAL CWVCencSingleSampleDecrypter::DecryptAndDecodeVideo(
    kodi::addon::CInstanceVideoCodec* codecInstance, const DEMUX_PACKET* sample)
{
  // if we have an picture waiting, or not yet get the dest buffer, do nothing
  if (m_videoFrames.size() == 4)
    return VC_ERROR;

  if (sample->cryptoInfo && sample->cryptoInfo->numSubSamples > 0 &&
      (!sample->cryptoInfo->clearBytes || !sample->cryptoInfo->cipherBytes))
  {
    return VC_ERROR;
  }

  cdm::InputBuffer_2 inputBuffer{};
  std::vector<cdm::SubsampleEntry> subsamples;

  media::ToCdmInputBuffer(sample, &subsamples, &inputBuffer);

  if (sample->iSize > 0)
    m_isDrained = false;

  //LICENSERENEWAL:
  CheckLicenseRenewal();

  media::CdmVideoFrame videoFrame;

  // DecryptAndDecodeFrame calls CdmAdapter::Allocate which calls Host->GetBuffer
  // that cast hostInstance to CInstanceVideoCodec to get the frame buffer
  // so we have temporary set the host instance
  m_cdmAdapter->SetCodecInstance(codecInstance);
  cdm::Status status = m_cdmAdapter->GetCDM()->DecryptAndDecodeFrame(inputBuffer, &videoFrame);
  m_cdmAdapter->ResetCodecInstance();

  if (status == cdm::Status::kSuccess)
  {
    std::list<media::CdmVideoFrame>::iterator f(m_videoFrames.begin());
    while (f != m_videoFrames.end() && f->Timestamp() < videoFrame.Timestamp())
    {
      ++f;
    }
    m_videoFrames.insert(f, videoFrame);
    return VC_NONE;
  }
  else if (status == cdm::Status::kNeedMoreData && inputBuffer.data)
  {
    return VC_NONE;
  }
  else if (status == cdm::Status::kNoKey)
  {
    LOG::LogF(LOGERROR, "Returned CDM status \"kNoKey\" for KID: %s",
              STRING::ToHexadecimal(inputBuffer.key_id, inputBuffer.key_id_size).c_str());
    return VC_EOF;
  }

  LOG::LogF(LOGDEBUG, "Returned CDM status: %i", status);
  return VC_ERROR;
}

VIDEOCODEC_RETVAL CWVCencSingleSampleDecrypter::VideoFrameDataToPicture(
    kodi::addon::CInstanceVideoCodec* codecInstance, VIDEOCODEC_PICTURE* picture)
{
  if (m_videoFrames.size() == 4 ||
      (m_videoFrames.size() > 0 && (picture->flags & VIDEOCODEC_PICTURE_FLAG_DRAIN)))
  {
    media::CdmVideoFrame& videoFrame(m_videoFrames.front());

    picture->width = videoFrame.Size().width;
    picture->height = videoFrame.Size().height;
    picture->pts = videoFrame.Timestamp();
    picture->decodedData = videoFrame.FrameBuffer()->Data();
    picture->decodedDataSize = videoFrame.FrameBuffer()->Size();
    picture->videoBufferHandle = static_cast<CdmFixedBuffer*>(videoFrame.FrameBuffer())->Buffer();

    for (size_t i = 0; i < cdm::VideoPlane::kMaxPlanes; ++i)
    {
      picture->planeOffsets[i] = videoFrame.PlaneOffset(static_cast<cdm::VideoPlane>(i));
      picture->stride[i] = videoFrame.Stride(static_cast<cdm::VideoPlane>(i));
    }
    picture->videoFormat = media::ToSSDVideoFormat(videoFrame.Format());
    videoFrame.SetFrameBuffer(nullptr); //marker for "No Picture"

    delete (CdmFixedBuffer*)(videoFrame.FrameBuffer());
    m_videoFrames.pop_front();

    return VC_PICTURE;
  }
  else if ((picture->flags & VIDEOCODEC_PICTURE_FLAG_DRAIN))
  {
    static DEMUX_PACKET drainSample{};
    if (m_isDrained || DecryptAndDecodeVideo(codecInstance, &drainSample) == VC_ERROR)
    {
      m_isDrained = true;
      return VC_EOF;
    }
    else
      return VC_NONE;
  }

  return VC_BUFFER;
}

void CWVCencSingleSampleDecrypter::ResetVideo()
{
  m_cdmAdapter->GetCDM()->ResetDecoder(cdm::kStreamTypeVideo);
  m_isDrained = true;
}

void CWVCencSingleSampleDecrypter::SetDefaultKeyId(const std::vector<uint8_t>& keyId)
{
  m_defaultKeyId = keyId;
}

void CWVCencSingleSampleDecrypter::AddKeyId(const std::vector<uint8_t>& keyId)
{
  WVSKEY key;
  key.m_keyId = keyId;
  key.status = cdm::KeyStatus::kUsable;

  if (std::find(m_keys.begin(), m_keys.end(), key) == m_keys.end())
  {
    m_keys.push_back(key);
  }
}
