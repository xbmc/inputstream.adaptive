/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MFCencSingleSampleDecrypter.h"

#include "MFDecrypter.h"
#include "utils/Base64Utils.h"
#include "utils/CurlUtils.h"
#include "utils/DigestMD5Utils.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"
#include "utils/XMLUtils.h"
#include "pugixml.hpp"

#include <mfcdm/MediaFoundationCdm.h>

#include <mutex>
#include <thread>

using namespace pugi;
using namespace kodi::tools;
using namespace UTILS;

CMFCencSingleSampleDecrypter::CMFCencSingleSampleDecrypter(CMFDecrypter& host,
                                                           std::vector<uint8_t>& pssh,
                                                           std::string_view defaultKeyId,
                                                           bool skipSessionMessage,
                                                           CryptoMode cryptoMode)
  : m_host(host),
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

  if (pssh.size() > 4096)
  {
    LOG::LogF(LOGERROR, "PSSH init data with length %u seems not to be cenc init data",
              pssh.size());
    return;
  }

  if (m_host.IsDebugSaveLicense())
  {
    const std::string debugFilePath =
          FILESYS::PathCombine(m_host.GetProfilePath(), "9A04F079-9840-4286-AB92-E65BE0885F95.init");

    std::string data{reinterpret_cast<const char*>(pssh.data()), pssh.size()};
    FILESYS::SaveFile(debugFilePath, data, true);
  }

  // No cenc init data with PSSH box format, create one
  if (memcmp(pssh.data() + 4, "pssh", 4) != 0)
  {
    // PSSH box version 0 (no kid's)
    static const uint8_t atomHeader[12] = {0x00, 0x00, 0x00, 0x00, 0x70, 0x73,
                                           0x73, 0x68, 0x00, 0x00, 0x00, 0x00};

    static const uint8_t playReadySystemId[16] = {0x9A, 0x04, 0xF0, 0x79, 0x98, 0x40, 0x42, 0x86,
                                                  0xAB, 0x92, 0xE6, 0x5B, 0xE0, 0x88, 0x5F, 0x95};

    std::vector<uint8_t> psshAtom;
    psshAtom.assign(atomHeader, atomHeader + 12); // PSSH Box header
    psshAtom.insert(psshAtom.end(), playReadySystemId, playReadySystemId + 16); // System ID
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

  m_host.GetCdm()->CreateSessionAndGenerateRequest(MFTemporary, MFCenc, m_pssh, this);

  if (sessionId.empty())
  {
    LOG::LogF(LOGERROR, "Cannot perform License update, no session available");
    return;
  }

}

CMFCencSingleSampleDecrypter::~CMFCencSingleSampleDecrypter()
{
}

void CMFCencSingleSampleDecrypter::ParsePlayReadyMessage(const std::vector<uint8_t>& message, 
                                                         std::string& challenge, 
                                                         std::map<std::string, std::string>& headers)
{
  xml_document doc;

  // Load wide string XML
  xml_parse_result parseRes = doc.load_buffer(message.data(), message.size());
  if (parseRes.status != status_ok)
  {
    LOG::LogF(LOGERROR, "Failed to parse PlayReady session message %i", parseRes.status);
    return;
  }

  if (m_host.IsDebugSaveLicense())
  {
    const std::string debugFilePath = FILESYS::PathCombine(
          m_host.GetProfilePath(), "9A04F079-9840-4286-AB92-E65BE0885F95.message");
  
    doc.save_file(debugFilePath.c_str());
  }

  xml_node nodeAcquisition = doc.first_element_by_path("PlayReadyKeyMessage/LicenseAcquisition");
  if (!nodeAcquisition)
  {
    LOG::LogF(LOGERROR, "Failed to get Playready's <LicenseAcquisition> tag element.");
    return;
  }

  xml_node nodeChallenge = nodeAcquisition.child("Challenge");
  if (!nodeChallenge)
  {
    LOG::LogF(LOGERROR, "Failed to get Playready's <Challenge> tag element.");
    return;
  }

  std::string encodingType; 
  encodingType = XML::GetAttrib(nodeChallenge, "encoding");
  if (encodingType != "base64encoded")
  {
    LOG::LogF(LOGERROR, "Unknown challenge encoding %s", encodingType);
    return;
  }

  challenge = BASE64::DecodeToStr(nodeChallenge.child_value());

  LOG::LogF(LOGDEBUG, "Challenge: encoding %s size %i", encodingType, challenge.size());

  if (xml_node nodeHeaders = nodeAcquisition.child("HttpHeaders"))
  {
    for (xml_node nodeHeader : nodeHeaders.children("HttpHeader"))
    {
      std::string name = nodeHeader.child_value("name");
      std::string value = nodeHeader.child_value("value");
      headers.insert({name, value});
    }
  }

  LOG::LogF(LOGDEBUG, "HttpHeaders: size %i", headers.size());
}

void CMFCencSingleSampleDecrypter::OnSessionMessage(std::string_view session,
                                                    const std::vector<uint8_t>& message,
                                                    std::string_view messageDestinationUrl)
{
  std::string challenge;
  std::map<std::string, std::string> playReadyHeaders;

  ParsePlayReadyMessage(message, challenge,
                        playReadyHeaders);

  sessionId = session;
  m_challenge.SetData(reinterpret_cast<const AP4_Byte*>(challenge.data()),
                      static_cast<AP4_Size>(challenge.size()));

  LOG::LogF(LOGDEBUG, "Playready message session ID: %s", sessionId.c_str());

  if (m_host.IsDebugSaveLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(
        m_host.GetProfilePath(), "9A04F079-9840-4286-AB92-E65BE0885F95.challenge");

    FILESYS::SaveFile(debugFilePath, challenge, true);
  }

  std::vector<std::string> blocks;
  if (!m_host.GetLicenseKey().empty())
  {
    blocks = StringUtils::Split(m_host.GetLicenseKey(), '|');
    if (blocks.size() != 4)
    {
      LOG::LogF(LOGERROR, "Wrong \"|\" blocks in license URL. Four blocks (req | header | body | "
                          "response) are expected in license URL");
      return;
    }
  }

  std::string destinationUrl;
  if (!blocks.empty())
  {
    destinationUrl = blocks[0];
  }
  else
  {
    destinationUrl = messageDestinationUrl;
  }

  CURL::CUrl file(destinationUrl);
  file.AddHeader("Expect", "");

  for (const auto& header: playReadyHeaders)
  {
    file.AddHeader(header.first, header.second);
  }

  //Process headers
  if(!blocks.empty())
  {
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
  }

  std::string encData{BASE64::Encode(challenge)};
  file.AddHeader("postdata", encData);

  int statusCode = file.Open();
  if (statusCode == -1 || statusCode >= 400)
  {
    LOG::Log(LOGERROR, "License server returned failure");
    return;
  }

  std::string response;

  CURL::ReadStatus downloadStatus = CURL::ReadStatus::CHUNK_READ;
  while (downloadStatus == CURL::ReadStatus::CHUNK_READ)
  {
    downloadStatus = file.Read(response);
  }

  if (downloadStatus == CURL::ReadStatus::ERROR)
  {
    LOG::LogF(LOGERROR, "Could not read full SessionMessage response");
    return;
  }

  if (m_host.IsDebugSaveLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(
        m_host.GetProfilePath(), "9A04F079-9840-4286-AB92-E65BE0885F95.response");
    FILESYS::SaveFile(debugFilePath, response, true);
  }

  m_host.GetCdm()->UpdateSession(
    sessionId, std::vector<uint8_t>(response.data(), response.data() + response.size()));
}

void CMFCencSingleSampleDecrypter::OnKeyChange(std::string_view sessionId,
                                               std::vector<std::unique_ptr<KeyInfo>> keys)
{
  LOG::LogF(LOGDEBUG, "Received %i keys", keys.size());
  for (const auto& key : keys)
  {
    char buf[36];
    buf[32] = 0;
    AP4_FormatHex(key->keyId.data(), key->keyId.size(), buf);

    LOG::LogF(LOGDEBUG, "Key: %s status: %i", buf, key->status);
  }
  m_keys = std::move(keys);
}

void CMFCencSingleSampleDecrypter::GetCapabilities(std::string_view key,
                                                   uint32_t media,
                                                   IDecrypter::DecrypterCapabilites& caps)
{
  caps = {IDecrypter::DecrypterCapabilites::SSD_SECURE_PATH |
              IDecrypter::DecrypterCapabilites::SSD_ANNEXB_REQUIRED,
          0, m_hdcpLimit};

  if (sessionId.empty())
  {
    LOG::LogF(LOGDEBUG, "Session empty");
    return;
  }

  if (m_keys.empty())
  {
    LOG::LogF(LOGDEBUG, "Keys empty");
    return;
  }

  if (!caps.hdcpLimit)
    caps.hdcpLimit = m_resolutionLimit;
}

const char* CMFCencSingleSampleDecrypter::GetSessionId()
{
  return sessionId.empty() ? nullptr : sessionId.c_str();
}

void CMFCencSingleSampleDecrypter::CloseSessionId()
{
  if (!sessionId.empty())
  {
    LOG::LogF(LOGDEBUG, "Closing MF session ID: %s", sessionId.c_str());
    //m_wvCdmAdapter.GetCdmAdapter()->CloseSession(++m_promiseId, sessionId.data(),
    //                                             sessionId.size());

    LOG::LogF(LOGDEBUG, "MF session ID %s closed", sessionId.c_str());
    sessionId.clear();
  }
}

AP4_DataBuffer CMFCencSingleSampleDecrypter::GetChallengeData()
{
  return m_challenge;
}

bool CMFCencSingleSampleDecrypter::HasKeyId(std::string_view keyId)
{
  if (!keyId.empty())
  {
    for (const std::unique_ptr<KeyInfo>& key : m_keys)
    {
      if (key->keyId == STRING::ToVecUint8(keyId))
        return true;
    }
  }
  return false;
}

AP4_Result CMFCencSingleSampleDecrypter::SetFragmentInfo(AP4_UI32 poolId,
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

AP4_UI32 CMFCencSingleSampleDecrypter::AddPool()
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


void CMFCencSingleSampleDecrypter::RemovePool(AP4_UI32 poolId)
{
  m_fragmentPool[poolId].m_nalLengthSize = 99;
  m_fragmentPool[poolId].m_key.clear();
}

void CMFCencSingleSampleDecrypter::RepackSubsampleData(AP4_DataBuffer& dataIn,
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

void CMFCencSingleSampleDecrypter::UnpackSubsampleData(AP4_DataBuffer& dataIn,
                                                       size_t& pos,
                                                       const unsigned int subsamplePos,
                                                       const AP4_UI16* bytesOfCleartextData,
                                                       const AP4_UI32* bytesOfEncryptedData)
{
  pos += bytesOfCleartextData[subsamplePos];
  m_decryptIn.AppendData(dataIn.GetData() + pos, bytesOfEncryptedData[subsamplePos]);
  pos += bytesOfEncryptedData[subsamplePos];
}

/*----------------------------------------------------------------------
|   CWVCencSingleSampleDecrypter::DecryptSampleData
+---------------------------------------------------------------------*/
AP4_Result CMFCencSingleSampleDecrypter::DecryptSampleData(AP4_UI32 poolId,
                                                           AP4_DataBuffer& dataIn,
                                                           AP4_DataBuffer& dataOut,
                                                           const AP4_UI08* iv,
                                                           unsigned int subsampleCount,
                                                           const AP4_UI16* bytesOfCleartextData,
                                                           const AP4_UI32* bytesOfEncryptedData)
{
  return AP4_ERROR_INVALID_PARAMETERS;
}

bool CMFCencSingleSampleDecrypter::OpenVideoDecoder(const VIDEOCODEC_INITDATA* initData)
{
  return false;
}

VIDEOCODEC_RETVAL CMFCencSingleSampleDecrypter::DecryptAndDecodeVideo(
    kodi::addon::CInstanceVideoCodec* codecInstance, const DEMUX_PACKET* sample)
{
  return VC_ERROR;
}

VIDEOCODEC_RETVAL CMFCencSingleSampleDecrypter::VideoFrameDataToPicture(
    kodi::addon::CInstanceVideoCodec* codecInstance, VIDEOCODEC_PICTURE* picture)
{
  return VC_BUFFER;
}

void CMFCencSingleSampleDecrypter::ResetVideo()
{
  //m_wvCdmAdapter.GetCdmAdapter()->ResetDecoder(cdm::kStreamTypeVideo);
  m_isDrained = true;
}

void CMFCencSingleSampleDecrypter::SetDefaultKeyId(std::string_view keyId)
{
  m_defaultKeyId = keyId;
}

void CMFCencSingleSampleDecrypter::AddKeyId(std::string_view keyId)
{
  std::unique_ptr<KeyInfo> key = std::make_unique<KeyInfo>(
    std::vector<uint8_t>(keyId.data(), keyId.data() + keyId.size()), MFKeyUsable);

  if (std::find(m_keys.begin(), m_keys.end(), key) == m_keys.end())
  {
    m_keys.push_back(std::move(key));
  }
}
