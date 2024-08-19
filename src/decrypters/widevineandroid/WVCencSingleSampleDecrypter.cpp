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
#include "WVCdmAdapter.h"
#include "WVDecrypter.h"
#include "jsmn.h"
#include "decrypters/Helpers.h"
#include "utils/Base64Utils.h"
#include "utils/CurlUtils.h"
#include "utils/DigestMD5Utils.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <thread>

using namespace UTILS;
using namespace kodi::tools;

CWVCencSingleSampleDecrypterA::CWVCencSingleSampleDecrypterA(CWVCdmAdapterA& drm,
                                                             std::vector<uint8_t>& pssh,
                                                             std::string_view optionalKeyParameter,
                                                             const std::vector<uint8_t>& defaultKeyId,
                                                             CWVDecrypterA* host)
  : m_mediaDrm(drm),
    m_isProvisioningRequested(false),
    m_isKeyUpdateRequested(false),
    m_hdcpLimit(0),
    m_resolutionLimit(0),
    m_defaultKeyId{defaultKeyId},
    m_host{host}
{
  SetParentIsOwner(false);

  if (pssh.size() < 4 || pssh.size() > 65535)
  {
    LOG::LogF(LOGERROR, "PSSH init data with length %zu seems not to be cenc init data",
              pssh.size());
    return;
  }

  m_pssh = pssh;
  // No cenc init data with PSSH box format, create one
  if (memcmp(pssh.data() + 4, "pssh", 4) != 0)
  {
    // PSSH box version 0 (no kid's)
    static const uint8_t atomHeader[12] = {0x00, 0x00, 0x00, 0x00, 0x70, 0x73,
                                           0x73, 0x68, 0x00, 0x00, 0x00, 0x00};

    std::vector<uint8_t> psshAtom;
    psshAtom.assign(atomHeader, atomHeader + 12); // PSSH Box header
    psshAtom.insert(psshAtom.end(), m_mediaDrm.GetKeySystem(), m_mediaDrm.GetKeySystem() + 16); // System ID
    // Add data size bytes
    psshAtom.resize(30, 0); // 2 zero bytes
    psshAtom.emplace_back(static_cast<uint8_t>((m_pssh.size()) >> 8));
    psshAtom.emplace_back(static_cast<uint8_t>(m_pssh.size()));

    psshAtom.insert(psshAtom.end(), m_pssh.begin(), m_pssh.end()); // Data
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
    FILESYS::SaveFile(debugFilePath, data, true);
  }

  m_initialPssh = m_pssh;

  if (!optionalKeyParameter.empty())
    m_optParams["PRCustomData"] = optionalKeyParameter;

  /*
  std::vector<char> pui = m_mediaDrm.GetMediaDrm()->getPropertyByteArray("provisioningUniqueId");
  xbmc_jnienv()->ExceptionClear();
  if (pui.size() > 0)
  {
    std::string encoded = BASE64::Encode(pui);
    m_optParams["CDMID"] = encoded;
  }
  */

  bool L3FallbackRequested = false;
RETRY_OPEN:
  m_sessionId = m_mediaDrm.GetMediaDrm()->openSession();
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
            m_mediaDrm.GetMediaDrm()->getPropertyString("securityLevel") == "L1")
        {
          LOG::LogF(LOGWARNING, "L1 provisioning failed - retrying with L3...");
          L3FallbackRequested = true;
          m_isProvisioningRequested = false;
          m_mediaDrm.GetMediaDrm()->setPropertyString("securityLevel", "L3");
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

  if (m_sessionId.size() == 0)
  {
    LOG::LogF(LOGERROR, "Unable to open DRM session");
    return;
  }

  memcpy(m_sessionIdChar, m_sessionId.data(), m_sessionId.size());
  m_sessionIdChar[m_sessionId.size()] = 0;

  if (m_mediaDrm.GetKeySystemType() != PLAYREADY)
  {
    int maxSecuritylevel = m_mediaDrm.GetMediaDrm()->getMaxSecurityLevel();
    xbmc_jnienv()->ExceptionClear();

    LOG::Log(LOGDEBUG, "Session ID: %s, Max security level: %d", m_sessionIdChar, maxSecuritylevel);
  }
}

CWVCencSingleSampleDecrypterA::~CWVCencSingleSampleDecrypterA()
{
  if (!m_sessionId.empty())
  {
    m_mediaDrm.GetMediaDrm()->removeKeys(m_sessionId);
    if (xbmc_jnienv()->ExceptionCheck())
    {
      LOG::LogF(LOGERROR, "removeKeys has raised an exception");
      xbmc_jnienv()->ExceptionClear();
    }
    m_mediaDrm.GetMediaDrm()->closeSession(m_sessionId);
    if (xbmc_jnienv()->ExceptionCheck())
    {
      LOG::LogF(LOGERROR, "closeSession has raised an exception");
      xbmc_jnienv()->ExceptionClear();
    }
  }
}

const char* CWVCencSingleSampleDecrypterA::GetSessionId()
{
  return m_sessionIdChar;
}

std::vector<char> CWVCencSingleSampleDecrypterA::GetChallengeData()
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
                                                    DecrypterCapabilites& caps)
{
  caps = {DecrypterCapabilites::SSD_SECURE_PATH | DecrypterCapabilites::SSD_ANNEXB_REQUIRED, 0,
          m_hdcpLimit};

  if (caps.hdcpLimit == 0)
    caps.hdcpLimit = m_resolutionLimit;

  // Note: Currently we check for L1 only, Kodi core at later time check if secure decoder is needed
  // by using requiresSecureDecoderComponent method of MediaDrm API
  // https://github.com/xbmc/xbmc/blob/Nexus/xbmc/cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodecAndroidMediaCodec.cpp#L639-L641
  if (m_mediaDrm.GetMediaDrm()->getPropertyString("securityLevel") == "L1")
  {
    caps.hdcpLimit = m_resolutionLimit; //No restriction
    caps.flags |= DecrypterCapabilites::SSD_SECURE_DECODER;
  }
  LOG::LogF(LOGDEBUG, "hdcpLimit: %i", caps.hdcpLimit);

  caps.hdcpVersion = 99;
}

bool CWVCencSingleSampleDecrypterA::ProvisionRequest()
{
  LOG::Log(LOGWARNING, "Provision data request (DRM:%p)", m_mediaDrm.GetMediaDrm());

  CJNIMediaDrmProvisionRequest request = m_mediaDrm.GetMediaDrm()->getProvisionRequest();
  if (xbmc_jnienv()->ExceptionCheck())
  {
    LOG::LogF(LOGERROR, "getProvisionRequest has raised an exception");
    xbmc_jnienv()->ExceptionClear();
    return false;
  }

  std::vector<char> provData = request.getData();
  std::string url = request.getDefaultUrl();

  LOG::Log(LOGDEBUG, "Provision data size: %lu, url: %s", provData.size(), url.c_str());

  std::string reqData("{\"signedRequest\":\"");
  reqData += std::string(provData.data(), provData.size());
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

  m_mediaDrm.GetMediaDrm()->provideProvisionResponse(provData);
  if (xbmc_jnienv()->ExceptionCheck())
  {
    LOG::LogF(LOGERROR, "provideProvisionResponse has raised an exception");
    xbmc_jnienv()->ExceptionClear();
    return false;
  }
  return true;
}

bool CWVCencSingleSampleDecrypterA::GetKeyRequest(std::vector<char>& keyRequestData)
{
  CJNIMediaDrmKeyRequest keyRequest = m_mediaDrm.GetMediaDrm()->getKeyRequest(
      m_sessionId, m_pssh, "video/mp4", CJNIMediaDrm::KEY_TYPE_STREAMING, m_optParams);

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

  if (m_mediaDrm.GetKeySystemType() != PLAYREADY)
  {
    int securityLevel = m_mediaDrm.GetMediaDrm()->getSecurityLevel(m_sessionId);
    xbmc_jnienv()->ExceptionClear();
    LOG::Log(LOGDEBUG, "Security level: %d", securityLevel);

    std::map<std::string, std::string> keyStatus =
        m_mediaDrm.GetMediaDrm()->queryKeyStatus(m_sessionId);
    LOG::Log(LOGDEBUG, "Key status (%ld):", keyStatus.size());
    for (auto const& ks : keyStatus)
    {
      LOG::Log(LOGDEBUG, "-> %s -> %s", ks.first.c_str(), ks.second.c_str());
    }
  }
  return true;
}

bool CWVCencSingleSampleDecrypterA::SendSessionMessage(const std::vector<char>& keyRequestData)
{
  std::vector<std::string> blocks{StringUtils::Split(m_mediaDrm.GetLicenseURL(), '|')};

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
    UTILS::FILESYS::SaveFile(debugFilePath, keyRequestData.data(), true);
  }

  //Process placeholder in GET String
  std::string::size_type insPos(blocks[0].find("{SSM}"));
  if (insPos != std::string::npos)
  {
    if (insPos > 0 && blocks[0][insPos - 1] == 'B')
    {
      std::string msgEncoded = BASE64::Encode(keyRequestData);
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
    md5.Update(keyRequestData.data(), static_cast<uint32_t>(keyRequestData.size()));
    md5.Finalize();
    blocks[0].replace(insPos, 6, md5.HexDigest());
  }

  CURL::CUrl file(blocks[0]);
  std::string response;
  std::string resLimit;
  std::string contentType;

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
      file.AddHeader(header[0], value);
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
      std::string::size_type psshPos(blocks[2].find("{PSSH}"));

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
        if (psshPos != std::string::npos)
          psshPos -= 2;
      }

      size_t sizeWritten(0);

      if (insPos > 0)
      {
        if (blocks[2][insPos - 1] == 'B' || blocks[2][insPos - 1] == 'b')
        {
          std::string msgEncoded = BASE64::Encode(keyRequestData);
          if (blocks[2][insPos - 1] == 'B')
          {
            msgEncoded = STRING::URLEncode(msgEncoded);
          }
          blocks[2].replace(insPos - 1, 6, msgEncoded);
          sizeWritten = msgEncoded.size();
        }
        else if (blocks[2][insPos - 1] == 'D')
        {
          std::string msgEncoded{STRING::ToDecimal(
              reinterpret_cast<const uint8_t*>(keyRequestData.data()), keyRequestData.size())};
          blocks[2].replace(insPos - 1, 6, msgEncoded);
          sizeWritten = msgEncoded.size();
        }
        else
        {
          blocks[2].replace(insPos - 1, 6, keyRequestData.data(), keyRequestData.size());
          sizeWritten = keyRequestData.size();
        }
      }
      else
      {
        LOG::Log(LOGERROR, "Unsupported License request template (body / ?{SSM})");
        return false;
      }

      if (sidPos != std::string::npos && insPos < sidPos)
        sidPos += sizeWritten, sidPos -= 6;

      if (kidPos != std::string::npos && insPos < kidPos)
        kidPos += sizeWritten, kidPos -= 6;

      if (psshPos != std::string::npos && insPos < psshPos)
        psshPos += sizeWritten, psshPos -= 6;

      sizeWritten = 0;

      if (sidPos != std::string::npos)
      {
        if (sidPos > 0)
        {
          if (blocks[2][sidPos - 1] == 'B' || blocks[2][sidPos - 1] == 'b')
          {
            std::string msgEncoded = BASE64::Encode(m_sessionId);
            if (blocks[2][sidPos - 1] == 'B')
            {
              msgEncoded = STRING::URLEncode(msgEncoded);
            }
            blocks[2].replace(sidPos - 1, 6, msgEncoded);
            sizeWritten = msgEncoded.size();
          }
          else
          {
            blocks[2].replace(sidPos - 1, 6, m_sessionId.data(), m_sessionId.size());
            sizeWritten = m_sessionId.size();
          }
        }
        else
        {
          LOG::Log(LOGERROR, "Unsupported License request template (body / ?{SID})");
          return false;
        }
      }

      if (kidPos != std::string::npos && sidPos < kidPos)
        kidPos += sizeWritten, kidPos -= 6;

      if (psshPos != std::string::npos && sidPos < psshPos)
        psshPos += sizeWritten, psshPos -= 6;

      size_t kidPlaceholderLen = 6;
      if (kidPos != std::string::npos)
      {
        if (blocks[2][kidPos - 1] == 'H')
        {
          std::string keyIdUUID{STRING::ToHexadecimal(m_defaultKeyId)};
          blocks[2].replace(kidPos - 1, 6, keyIdUUID.c_str(), 32);
        }
        else
        {
          std::string kidUUID{DRM::ConvertKidBytesToUUID(m_defaultKeyId)};
          blocks[2].replace(kidPos, 5, kidUUID.c_str(), 36);
          kidPlaceholderLen = 5;
        }
      }

      if (psshPos != std::string::npos && kidPos < psshPos)
        psshPos += sizeWritten, psshPos -= kidPlaceholderLen;

      if (psshPos != std::string::npos)
      {
        std::string msgEncoded = BASE64::Encode(m_initialPssh);
        if (blocks[2][psshPos - 1] == 'B')
        {
          msgEncoded = STRING::URLEncode(msgEncoded);
        }
        blocks[2].replace(psshPos - 1, 7, msgEncoded);
        sizeWritten = msgEncoded.size();
      }

      if (fullDecode)
      {
        std::string msgEncoded = BASE64::Encode(blocks[2]);
        if (fullDecode == 'B')
        {
          msgEncoded = STRING::URLEncode(msgEncoded);
        }
        blocks[2] = msgEncoded;
      }

      if (CSrvBroker::GetSettings().IsDebugLicense())
      {
        std::string debugFilePath = FILESYS::PathCombine(
            m_host->GetLibraryPath(), "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.postdata");
        UTILS::FILESYS::SaveFile(debugFilePath, blocks[2], true);
      }
    }

    std::string encData{BASE64::Encode(blocks[2])};
    file.AddHeader("postdata", encData.c_str());
  }

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
    std::string::size_type posMax = resLimit.find("max=");
    if (posMax != std::string::npos)
      m_resolutionLimit = std::atoi(resLimit.data() + (posMax + 4));
  }

  if (downloadStatus == CURL::ReadStatus::ERROR)
  {
    LOG::LogF(LOGERROR, "Could not read full SessionMessage response");
    return false;
  }
  else if (response.empty())
  {
    LOG::LogF(LOGERROR, "Empty SessionMessage response - invalid");
    return false;
  }

  if (m_mediaDrm.GetKeySystemType() == PLAYREADY &&
      response.find("<LicenseNonce>") == std::string::npos)
  {
    std::string::size_type dstPos(response.find("</Licenses>"));
    std::string challenge(keyRequestData.data(), keyRequestData.size());
    std::string::size_type srcPosS(challenge.find("<LicenseNonce>"));
    if (dstPos != std::string::npos && srcPosS != std::string::npos)
    {
      LOG::Log(LOGDEBUG, "Inserting <LicenseNonce>");
      std::string::size_type srcPosE(challenge.find("</LicenseNonce>", srcPosS));
      if (srcPosE != std::string::npos)
        response.insert(dstPos + 11, challenge.c_str() + srcPosS, srcPosE - srcPosS + 15);
    }
  }

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(
        m_host->GetLibraryPath(), "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.response");
    UTILS::FILESYS::SaveFile(debugFilePath, response, true);
  }

  if (!blocks[3].empty() && blocks[3][0] != 'R' &&
      (keyRequestData.size() > 2 ||
       contentType.find("application/octet-stream") == std::string::npos))
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
          m_hdcpLimit = atoi((response.c_str() + tokens[i + 1].start));
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
        response = response.substr(tokens[i + 1].start, tokens[i + 1].end - tokens[i + 1].start);

        if (blocks[3][dataPos - 1] == 'B')
        {
          response = BASE64::DecodeToStr(response);
        }
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
          response = std::string(response.c_str() + payloadPos, response.c_str() + response.size());
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
      response = BASE64::DecodeToStr(response);
    }
    else
    {
      LOG::LogF(LOGERROR, "Unsupported License request template (response)");
      return false;
    }
  }

  m_keySetId = m_mediaDrm.GetMediaDrm()->provideKeyResponse(
      m_sessionId, std::vector<char>(response.data(), response.data() + response.size()));
  if (xbmc_jnienv()->ExceptionCheck())
  {
    LOG::LogF(LOGERROR, "provideKeyResponse has raised an exception");
    xbmc_jnienv()->ExceptionClear();
    return false;
  }

  if (keyRequestData.size() == 2)
    m_mediaDrm.SaveServiceCertificate();

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
  if (!m_mediaDrm.GetMediaDrm())
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
