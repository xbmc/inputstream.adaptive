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
#include "WVDecrypter.h"
#include "cdm/media/cdm/cdm_adapter.h"
#include "jsmn.h"
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

void CWVCencSingleSampleDecrypter::SetSession(const char* session,
                                              uint32_t sessionSize,
                                              const uint8_t* data,
                                              size_t dataSize)
{
  std::lock_guard<std::mutex> lock(m_renewalLock);

  m_strSession = std::string(session, sessionSize);
  m_challenge.SetData(data, dataSize);
  LOG::LogF(LOGDEBUG, "Opened widevine session ID: %s", m_strSession.c_str());
}

CWVCencSingleSampleDecrypter::CWVCencSingleSampleDecrypter(CWVCdmAdapter& drm,
                                                           std::vector<uint8_t>& pssh,
                                                           const std::vector<uint8_t>& defaultKeyId,
                                                           bool skipSessionMessage,
                                                           CryptoMode cryptoMode,
                                                           CWVDecrypter* host)
  : m_wvCdmAdapter(drm),
    m_pssh(pssh),
    m_hdcpVersion(99),
    m_hdcpLimit(0),
    m_resolutionLimit(0),
    m_promiseId(1),
    m_isDrained(true),
    m_defaultKeyId(defaultKeyId),
    m_EncryptionMode(cryptoMode),
    m_host(host)
{
  SetParentIsOwner(false);

  if (pssh.size() < 4 || pssh.size() > 4096)
  {
    LOG::LogF(LOGERROR, "PSSH init data with length %zu seems not to be cenc init data",
              pssh.size());
    return;
  }

  m_wvCdmAdapter.insertssd(this);

  // No cenc init data with PSSH box format, create one
  if (memcmp(pssh.data() + 4, "pssh", 4) != 0)
  {
    // This will request a new session and initializes session_id and message members in cdm_adapter.
    // message will be used to create a license request in the step after CreateSession call.
    // Initialization data is the widevine cdm pssh code in google proto style found in mpd schemeIdUri

    // PSSH box version 0 (no kid's)
    static const uint8_t atomHeader[12] = {0x00, 0x00, 0x00, 0x00, 0x70, 0x73,
                                           0x73, 0x68, 0x00, 0x00, 0x00, 0x00};

    static const uint8_t widevineSystemId[16] = {0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce,
                                                 0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed};

    std::vector<uint8_t> psshAtom;
    psshAtom.assign(atomHeader, atomHeader + 12); // PSSH Box header
    psshAtom.insert(psshAtom.end(), widevineSystemId, widevineSystemId + 16); // System ID
    // Add data size bytes
    psshAtom.resize(30, 0); // 2 zero bytes
    psshAtom.emplace_back(static_cast<uint8_t>((pssh.size()) >> 8));
    psshAtom.emplace_back(static_cast<uint8_t>(pssh.size()));

    psshAtom.insert(psshAtom.end(), pssh.begin(), pssh.end()); // Data
    // Update box size
    psshAtom[2] = static_cast<uint8_t>(psshAtom.size() >> 8);
    psshAtom[3] = static_cast<uint8_t>(psshAtom.size());
    m_pssh = psshAtom;
  }

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string debugFilePath =
        FILESYS::PathCombine(m_host->GetLibraryPath(), "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.init");

    std::string data{reinterpret_cast<const char*>(m_pssh.data()), m_pssh.size()};
    UTILS::FILESYS::SaveFile(debugFilePath, data, true);
  }

  drm.GetCdmAdapter()->CreateSessionAndGenerateRequest(m_promiseId++, cdm::SessionType::kTemporary,
                                                       cdm::InitDataType::kCenc, m_pssh.data(),
                                                       static_cast<uint32_t>(m_pssh.size()));

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

  while (m_challenge.GetDataSize() > 0 && SendSessionMessage())
    ;
}

CWVCencSingleSampleDecrypter::~CWVCencSingleSampleDecrypter()
{
  m_wvCdmAdapter.removessd(this);
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

const char* CWVCencSingleSampleDecrypter::GetSessionId()
{
  return m_strSession.empty() ? nullptr : m_strSession.c_str();
}

void CWVCencSingleSampleDecrypter::CloseSessionId()
{
  if (!m_strSession.empty())
  {
    LOG::LogF(LOGDEBUG, "Closing widevine session ID: %s", m_strSession.c_str());
    m_wvCdmAdapter.GetCdmAdapter()->CloseSession(++m_promiseId, m_strSession.data(),
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
  std::vector<std::string> blocks{StringUtils::Split(m_wvCdmAdapter.GetLicenseURL(), '|')};

  if (blocks.size() != 4)
  {
    LOG::LogF(LOGERROR, "Wrong \"|\" blocks in license URL. Four blocks (req | header | body | "
                        "response) are expected in license URL");
    return false;
  }

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(
        m_host->GetLibraryPath(), "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.challenge");
    std::string data{reinterpret_cast<const char*>(m_challenge.GetData()),
                     m_challenge.GetDataSize()};
    UTILS::FILESYS::SaveFile(debugFilePath, data, true);
  }

  //Process placeholder in GET String
  std::string::size_type insPos(blocks[0].find("{SSM}"));
  if (insPos != std::string::npos)
  {
    if (insPos > 0 && blocks[0][insPos - 1] == 'B')
    {
      std::string msgEncoded{BASE64::Encode(m_challenge.GetData(), m_challenge.GetDataSize())};
      msgEncoded = STRING::URLEncode(msgEncoded);
      blocks[0].replace(insPos - 1, 6, msgEncoded);
    }
    else
    {
      LOG::Log(LOGERROR, "Unsupported License request template (command)");
      return false;
    }
  }

  insPos = blocks[0].find("{HASH}");
  if (insPos != std::string::npos)
  {
    DIGEST::MD5 md5;
    md5.Update(m_challenge.GetData(), m_challenge.GetDataSize());
    md5.Finalize();
    blocks[0].replace(insPos, 6, md5.HexDigest());
  }

  CURL::CUrl file{blocks[0].c_str()};
  file.AddHeader("Expect", "");

  std::string response;
  std::string resLimit;
  std::string contentType;
  char buf[2048];
  bool serverCertRequest;

  //Process headers
  std::vector<std::string> headers{StringUtils::Split(blocks[1], '&')};
  for (std::string& headerStr : headers)
  {
    std::vector<std::string> header{StringUtils::Split(headerStr, '=')};
    if (!header.empty())
    {
      StringUtils::Trim(header[0]);
      std::string value;
      if (header.size() > 1)
      {
        StringUtils::Trim(header[1]);
        value = STRING::URLDecode(header[1]);
      }
      file.AddHeader(header[0].c_str(), value.c_str());
    }
  }

  //Process body
  if (!blocks[2].empty())
  {
    if (blocks[2][0] == '%')
      blocks[2] = STRING::URLDecode(blocks[2]);

    insPos = blocks[2].find("{SSM}");
    if (insPos != std::string::npos)
    {
      std::string::size_type sidPos(blocks[2].find("{SID}"));
      std::string::size_type kidPos(blocks[2].find("{KID}"));

      char fullDecode = 0;
      if (insPos > 1 && sidPos > 1 && kidPos > 1 && (blocks[2][0] == 'b' || blocks[2][0] == 'B') &&
          blocks[2][1] == '{')
      {
        fullDecode = blocks[2][0];
        blocks[2] = blocks[2].substr(2, blocks[2].size() - 3);
        insPos -= 2;
        if (kidPos != std::string::npos)
          kidPos -= 2;
        if (sidPos != std::string::npos)
          sidPos -= 2;
      }

      size_t size_written(0);

      if (insPos > 0)
      {
        if (blocks[2][insPos - 1] == 'B' || blocks[2][insPos - 1] == 'b')
        {
          std::string msgEncoded{BASE64::Encode(m_challenge.GetData(), m_challenge.GetDataSize())};
          if (blocks[2][insPos - 1] == 'B')
          {
            msgEncoded = STRING::URLEncode(msgEncoded);
          }
          blocks[2].replace(insPos - 1, 6, msgEncoded);
          size_written = msgEncoded.size();
        }
        else if (blocks[2][insPos - 1] == 'D')
        {
          std::string msgEncoded{
              STRING::ToDecimal(m_challenge.GetData(), m_challenge.GetDataSize())};
          blocks[2].replace(insPos - 1, 6, msgEncoded);
          size_written = msgEncoded.size();
        }
        else
        {
          blocks[2].replace(insPos - 1, 6, reinterpret_cast<const char*>(m_challenge.GetData()),
                            m_challenge.GetDataSize());
          size_written = m_challenge.GetDataSize();
        }
      }
      else
      {
        LOG::Log(LOGERROR, "Unsupported License request template (body / ?{SSM})");
        return false;
      }

      if (sidPos != std::string::npos && insPos < sidPos)
        sidPos += size_written, sidPos -= 6;

      if (kidPos != std::string::npos && insPos < kidPos)
        kidPos += size_written, kidPos -= 6;

      size_written = 0;

      if (sidPos != std::string::npos)
      {
        if (sidPos > 0)
        {
          if (blocks[2][sidPos - 1] == 'B' || blocks[2][sidPos - 1] == 'b')
          {
            std::string msgEncoded{BASE64::Encode(m_strSession)};

            if (blocks[2][sidPos - 1] == 'B')
            {
              msgEncoded = STRING::URLEncode(msgEncoded);
            }

            blocks[2].replace(sidPos - 1, 6, msgEncoded);
            size_written = msgEncoded.size();
          }
          else
          {
            blocks[2].replace(sidPos - 1, 6, m_strSession.data(), m_strSession.size());
            size_written = m_strSession.size();
          }
        }
        else
        {
          LOG::LogF(LOGERROR, "Unsupported License request template (body / ?{SID})");
          return false;
        }
      }

      if (kidPos != std::string::npos)
      {
        if (sidPos < kidPos)
          kidPos += size_written, kidPos -= 6;

        if (blocks[2][kidPos - 1] == 'H')
        {
          std::string keyIdUUID{STRING::ToHexadecimal(m_defaultKeyId)};
          blocks[2].replace(kidPos - 1, 6, keyIdUUID.c_str(), 32);
        }
        else
        {
          std::string kidUUID{DRM::ConvertKidBytesToUUID(m_defaultKeyId)};
          blocks[2].replace(kidPos, 5, kidUUID.c_str(), 36);
        }
      }

      if (fullDecode)
      {
        std::string msgEncoded{BASE64::Encode(blocks[2])};
        if (fullDecode == 'B')
        {
          msgEncoded = STRING::URLEncode(msgEncoded);
        }
        blocks[2] = msgEncoded;
      }
    }

    std::string encData{BASE64::Encode(blocks[2])};
    //! @todo: inappropriate use of "postdata" header, use CURL::CUrl for post request
    file.AddHeader("postdata", encData.c_str());
  }

  serverCertRequest = m_challenge.GetDataSize() == 2;
  m_challenge.SetDataSize(0);

  int statusCode = file.Open();
  if (statusCode == -1 || statusCode >= 400)
  {
    LOG::Log(LOGERROR, "License server returned failure (HTTP error %i)", statusCode);
    return false;
  }

  CURL::ReadStatus downloadStatus = CURL::ReadStatus::CHUNK_READ;
  while (downloadStatus == CURL::ReadStatus::CHUNK_READ)
  {
    downloadStatus = file.Read(response);
  }

  resLimit = file.GetResponseHeader("X-Limit-Video");
  contentType = file.GetResponseHeader("Content-Type");

  if (!resLimit.empty())
  {
    std::string::size_type posMax = resLimit.find("max="); // log/check this
    if (posMax != std::string::npos)
      m_resolutionLimit = std::atoi(resLimit.data() + (posMax + 4));
  }

  if (downloadStatus == CURL::ReadStatus::ERROR)
  {
    LOG::LogF(LOGERROR, "Could not read full SessionMessage response");
    return false;
  }

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(
        m_host->GetLibraryPath(), "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.response");
    FILESYS::SaveFile(debugFilePath, response, true);
  }

  if (serverCertRequest && contentType.find("application/octet-stream") == std::string::npos)
    serverCertRequest = false;

  if (!blocks[3].empty() && blocks[3][0] != 'R' && !serverCertRequest)
  {
    if (blocks[3][0] == 'J' || (blocks[3].size() > 1 && blocks[3][0] == 'B' && blocks[3][1] == 'J'))
    {
      int dataPos = 2;

      if (response.size() >= 3 && blocks[3][0] == 'B')
      {
        response = BASE64::DecodeToStr(response);
        dataPos = 3;
      }

      jsmn_parser jsn;
      jsmntok_t tokens[256];

      jsmn_init(&jsn);
      int i(0), numTokens = jsmn_parse(&jsn, response.c_str(), response.size(), tokens, 256);

      std::vector<std::string> jsonVals{StringUtils::Split(blocks[3].substr(dataPos), ';')};

      // Find HDCP limit
      if (jsonVals.size() > 1)
      {
        for (; i < numTokens; ++i)
          if (tokens[i].type == JSMN_STRING && tokens[i].size == 1 &&
              jsonVals[1].size() == static_cast<unsigned int>(tokens[i].end - tokens[i].start) &&
              strncmp(response.c_str() + tokens[i].start, jsonVals[1].c_str(),
                      tokens[i].end - tokens[i].start) == 0)
            break;
        if (i < numTokens)
          m_hdcpLimit = std::atoi((response.c_str() + tokens[i + 1].start));
      }
      // Find license key
      if (jsonVals.size() > 0)
      {
        for (i = 0; i < numTokens; ++i)
          if (tokens[i].type == JSMN_STRING && tokens[i].size == 1 &&
              jsonVals[0].size() == static_cast<unsigned int>(tokens[i].end - tokens[i].start) &&
              strncmp(response.c_str() + tokens[i].start, jsonVals[0].c_str(),
                      tokens[i].end - tokens[i].start) == 0)
          {
            if (i + 1 < numTokens && tokens[i + 1].type == JSMN_ARRAY && tokens[i + 1].size == 1)
              ++i;
            break;
          }
      }
      else
        i = numTokens;

      if (i < numTokens)
      {
        std::string respData{
            response.substr(tokens[i + 1].start, tokens[i + 1].end - tokens[i + 1].start)};

        if (blocks[3][dataPos - 1] == 'B')
        {
          respData = BASE64::DecodeToStr(respData);
        }

        m_wvCdmAdapter.GetCdmAdapter()->UpdateSession(
            ++m_promiseId, m_strSession.data(), m_strSession.size(),
            reinterpret_cast<const uint8_t*>(respData.c_str()), respData.size());
      }
      else
      {
        LOG::LogF(LOGERROR, "Unable to find %s in JSON string", blocks[3].c_str() + 2);
        return false;
      }
    }
    else if (blocks[3][0] == 'H' && blocks[3].size() >= 2)
    {
      //Find the payload
      std::string::size_type payloadPos = response.find("\r\n\r\n");
      if (payloadPos != std::string::npos)
      {
        payloadPos += 4;
        if (blocks[3][1] == 'B')
          m_wvCdmAdapter.GetCdmAdapter()->UpdateSession(
              ++m_promiseId, m_strSession.data(), m_strSession.size(),
              reinterpret_cast<const uint8_t*>(response.c_str() + payloadPos),
              response.size() - payloadPos);
        else
        {
          LOG::LogF(LOGERROR, "Unsupported HTTP payload data type definition");
          return false;
        }
      }
      else
      {
        LOG::LogF(LOGERROR, "Unable to find HTTP payload in response");
        return false;
      }
    }
    else if (blocks[3][0] == 'B' && blocks[3].size() == 1)
    {
      std::string decRespData{BASE64::DecodeToStr(response)};

      m_wvCdmAdapter.GetCdmAdapter()->UpdateSession(
          ++m_promiseId, m_strSession.data(), m_strSession.size(),
          reinterpret_cast<const uint8_t*>(decRespData.c_str()), decRespData.size());
    }
    else
    {
      LOG::LogF(LOGERROR, "Unsupported License request template (response)");
      return false;
    }
  }
  else // its binary - simply push the returned data as update
  {
    m_wvCdmAdapter.GetCdmAdapter()->UpdateSession(
        ++m_promiseId, m_strSession.data(), m_strSession.size(),
        reinterpret_cast<const uint8_t*>(response.data()), response.size());
  }

  if (m_keys.empty())
  {
    LOG::LogF(LOGERROR, "License update not successful (no keys)");
    CloseSessionId();
    return false;
  }

  LOG::Log(LOGDEBUG, "License update successful");
  return true;
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
  if (!m_wvCdmAdapter.GetCdmAdapter())
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
    ret = m_wvCdmAdapter.GetCdmAdapter()->Decrypt(cdmIn, &cdmOut);

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

    m_wvCdmAdapter.GetCdmAdapter()->DeinitializeDecoder(cdm::StreamType::kStreamTypeVideo);
  }

  m_currentVideoDecConfig = vconfig;

  cdm::Status ret = m_wvCdmAdapter.GetCdmAdapter()->InitializeVideoDecoder(vconfig);
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
  cdm::Status status =
      m_wvCdmAdapter.DecryptAndDecodeFrame(inputBuffer, &videoFrame, codecInstance);

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
  m_wvCdmAdapter.GetCdmAdapter()->ResetDecoder(cdm::kStreamTypeVideo);
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
