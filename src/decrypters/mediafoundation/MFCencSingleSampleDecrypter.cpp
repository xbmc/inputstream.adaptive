/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MFCencSingleSampleDecrypter.h"

#include "../../utils/Base64Utils.h"
#include "../../utils/CurlUtils.h"
#include "../../utils/DigestMD5Utils.h"
#include "../../utils/FileUtils.h"
#include "../../utils/StringUtils.h"
#include "../../utils/Utils.h"
#include "../../utils/log.h"

#include "MFDecrypter.h"
#include "mfcdm/MediaFoundationCdm.h"

#include <mutex>
#include <thread>

using namespace kodi::tools;
using namespace UTILS;

void CMFCencSingleSampleDecrypter::SetSession(const char* session,
                                              uint32_t sessionSize,
                                              const uint8_t* data,
                                              size_t dataSize)
{
  std::lock_guard<std::mutex> lock(m_renewalLock);

  m_strSession = std::string(session, sessionSize);
  m_challenge.SetData(data, dataSize);
  LOG::LogF(LOGDEBUG, "Opened widevine session ID: %s", m_strSession.c_str());
}

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

  //m_wvCdmAdapter.insertssd(this);

  if (m_host.IsDebugSaveLicense())
  {
    std::string debugFilePath =
        FILESYS::PathCombine(m_host.GetProfilePath(), "9A04F079-9840-4286-AB92-E65BE0885F95.init");

    std::string data{reinterpret_cast<const char*>(pssh.data()), pssh.size()};
    UTILS::FILESYS::SaveFile(debugFilePath, data, true);
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

  m_host.GetCdm()->CreateSessionAndGenerateRequest(
      m_promiseId++, cdm::SessionType::kTemporary, cdm::InitDataType::kCenc, 
    m_pssh.data(), m_pssh.size());

  int retryCount = 0;
  while (m_strSession.empty() && ++retryCount < 100)
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

CMFCencSingleSampleDecrypter::~CMFCencSingleSampleDecrypter()
{
  //m_wvCdmAdapter.removessd(this);
}

void CMFCencSingleSampleDecrypter::GetCapabilities(std::string_view key,
                                                   uint32_t media,
                                                   IDecrypter::DecrypterCapabilites& caps)
{
  caps = {0, m_hdcpVersion, m_hdcpLimit};

  if (m_strSession.empty())
  {
    LOG::LogF(LOGDEBUG, "Session empty");
    return;
  }

  caps.flags = IDecrypter::DecrypterCapabilites::SSD_SUPPORTS_DECODING;

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
  return m_strSession.empty() ? nullptr : m_strSession.c_str();
}

void CMFCencSingleSampleDecrypter::CloseSessionId()
{
  if (!m_strSession.empty())
  {
    LOG::LogF(LOGDEBUG, "Closing widevine session ID: %s", m_strSession.c_str());
    //m_wvCdmAdapter.GetCdmAdapter()->CloseSession(++m_promiseId, m_strSession.data(),
    //                                             m_strSession.size());

    LOG::LogF(LOGDEBUG, "MF session ID %s closed", m_strSession.c_str());
    m_strSession.clear();
  }
}

AP4_DataBuffer CMFCencSingleSampleDecrypter::GetChallengeData()
{
  return m_challenge;
}

void CMFCencSingleSampleDecrypter::CheckLicenseRenewal()
{
  {
    std::lock_guard<std::mutex> lock(m_renewalLock);
    if (!m_challenge.GetDataSize())
      return;
  }
  SendSessionMessage();
}

bool CMFCencSingleSampleDecrypter::SendSessionMessage()
{
  // StringUtils::Split(m_wvCdmAdapter.GetLicenseURL(), '|')
  std::vector<std::string> blocks{};

  if (blocks.size() != 4)
  {
    LOG::LogF(LOGERROR, "Wrong \"|\" blocks in license URL. Four blocks (req | header | body | "
                        "response) are expected in license URL");
    return false;
  }

  if (m_host.IsDebugSaveLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(
        m_host.GetProfilePath(), "9A04F079-9840-4286-AB92-E65BE0885F95.challenge");
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
          std::string keyIdUUID{StringUtils::ToHexadecimal(m_defaultKeyId)};
          blocks[2].replace(kidPos - 1, 6, keyIdUUID.c_str(), 32);
        }
        else
        {
          std::string kidUUID{ConvertKIDtoUUID(m_defaultKeyId)};
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
    file.AddHeader("postdata", encData.c_str());
  }

  serverCertRequest = m_challenge.GetDataSize() == 2;
  m_challenge.SetDataSize(0);

  if (!file.Open())
  {
    LOG::Log(LOGERROR, "License server returned failure");
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

  if (m_host.IsDebugSaveLicense())
  {
    std::string debugFilePath = FILESYS::PathCombine(
        m_host.GetProfilePath(), "9A04F079-9840-4286-AB92-E65BE0885F95.response");
    FILESYS::SaveFile(debugFilePath, response, true);
  }

  if (serverCertRequest && contentType.find("application/octet-stream") == std::string::npos)
    serverCertRequest = false;

  //m_wvCdmAdapter.GetCdmAdapter()->UpdateSession(
  //    ++m_promiseId, m_strSession.data(), m_strSession.size(),
  //    reinterpret_cast<const uint8_t*>(response.data()), response.size());

  if (m_keys.empty())
  {
    LOG::LogF(LOGERROR, "License update not successful (no keys)");
    CloseSessionId();
    return false;
  }

  LOG::Log(LOGDEBUG, "License update successful");
  return true;
}

void CMFCencSingleSampleDecrypter::AddSessionKey(const uint8_t* data,
                                                 size_t dataSize,
                                                 uint32_t status)
{
  WVSKEY key;
  std::vector<WVSKEY>::iterator res;

  key.m_keyId = std::string((const char*)data, dataSize);
  if ((res = std::find(m_keys.begin(), m_keys.end(), key)) == m_keys.end())
    res = m_keys.insert(res, key);
  res->status = static_cast<cdm::KeyStatus>(status);
}

bool CMFCencSingleSampleDecrypter::HasKeyId(std::string_view keyid)
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

void CMFCencSingleSampleDecrypter::LogDecryptError(const cdm::Status status, const AP4_UI08* key)
{
  char buf[36];
  buf[32] = 0;
  AP4_FormatHex(key, 16, buf);
  LOG::LogF(LOGDEBUG, "Decrypt failed with error: %d and key: %s", status, buf);
}

void CMFCencSingleSampleDecrypter::SetCdmSubsamples(std::vector<cdm::SubsampleEntry>& subsamples,
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

void CMFCencSingleSampleDecrypter::SetInput(cdm::InputBuffer_2& cdmInputBuffer,
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
  cdmInputBuffer.key_id_size = 16;
  cdmInputBuffer.subsamples = subsamples.data();
  //cdmInputBuffer.encryption_scheme = media::ToCdmEncryptionScheme(fragInfo.m_cryptoInfo.m_mode);
  cdmInputBuffer.timestamp = 0;
  cdmInputBuffer.pattern = {fragInfo.m_cryptoInfo.m_cryptBlocks,
                            fragInfo.m_cryptoInfo.m_skipBlocks};
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
  WVSKEY key;
  key.m_keyId = keyId;
  key.status = cdm::KeyStatus::kUsable;

  if (std::find(m_keys.begin(), m_keys.end(), key) == m_keys.end())
  {
    m_keys.push_back(key);
  }
}
