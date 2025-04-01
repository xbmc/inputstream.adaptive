/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ClearKeyCencSingleSampleDecrypter.h"

#include "ClearKeyDecrypter.h"
#include "CompSettings.h"
#include "SrvBroker.h"
#include "utils/Base64Utils.h"
#include "utils/CurlUtils.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/log.h"

#include <algorithm>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

using namespace UTILS;

uint32_t CClearKeyCencSingleSampleDecrypter::g_sessionIdCount = 1;

CClearKeyCencSingleSampleDecrypter::CClearKeyCencSingleSampleDecrypter(
    std::string_view licenseUri,
    const std::map<std::string, std::string>& licenseHeaders,
    const std::vector<uint8_t>& defaultKeyId,
    CClearKeyDecrypter* host)
  : m_host(host)
{
  if (licenseUri.empty())
  {
    LOG::LogF(LOGERROR, "Cannot decrypt, the license server URI is missing");
    return;
  }

  std::string licenseData;
  std::vector<uint8_t> uriData;

  if (URL::GetUriByteData(licenseUri, uriData)) // Provided license data in URI format
  {
    licenseData.assign(uriData.begin(), uriData.end());
  }
  else // Make the request to the server by using URL
  {
    const std::string postData = CreateLicenseRequest(defaultKeyId);

    if (CSrvBroker::GetSettings().IsDebugLicense())
    {
      const std::string debugFilePath =
          FILESYS::PathCombine(m_host->GetLibraryPath(), "ClearKey.init");
      FILESYS::SaveFile(debugFilePath, postData.c_str(), true);
    }

    CURL::CUrl curl{licenseUri, postData};
    curl.AddHeader("Accept", "application/json");
    curl.AddHeader("Content-Type", "application/json");
    curl.AddHeaders(licenseHeaders);

    std::string response;
    int statusCode = curl.Open();
    if (statusCode == -1 || statusCode >= 400)
    {
      LOG::Log(LOGERROR, "License server returned failure (HTTP error %i)", statusCode);
      return;
    }

    if (curl.Read(response) != CURL::ReadStatus::IS_EOF)
    {
      LOG::LogF(LOGERROR, "Could not read the license server response");
      return;
    }

    if (CSrvBroker::GetSettings().IsDebugLicense())
    {
      const std::string debugFilePath =
          FILESYS::PathCombine(m_host->GetLibraryPath(), "ClearKey.response");
      FILESYS::SaveFile(debugFilePath, response, true);
    }

    licenseData = response;
  }

  if (!ParseLicenseResponse(licenseData))
  {
    LOG::LogF(LOGERROR, "Could not parse the license data");
    return;
  }

  const std::string b64DefaultKeyId = BASE64::Encode(defaultKeyId);
  if (!STRING::KeyExists(m_keyPairs, b64DefaultKeyId))
  {
    LOG::LogF(LOGERROR, "Key not found on license data");
    return;
  }

  const std::vector<uint8_t> keyBytes = BASE64::Decode(m_keyPairs[b64DefaultKeyId]);

  InitDecrypter(defaultKeyId, keyBytes);
}

CClearKeyCencSingleSampleDecrypter::CClearKeyCencSingleSampleDecrypter(
    const std::vector<uint8_t>& initData,
    const std::vector<uint8_t>& defaultKeyId,
    CClearKeyDecrypter* host)
  : m_host(host)
{
  std::vector<uint8_t> hexKey;
  // Currently HLS manifest only support this
  // and the init data should contain only the key
  hexKey = initData;

  InitDecrypter(defaultKeyId, hexKey);
}

void CClearKeyCencSingleSampleDecrypter::InitDecrypter(const std::vector<uint8_t>& defaultKeyId,
                                                         const std::vector<uint8_t>& key)
{
  AP4_CencSingleSampleDecrypter::Create(AP4_CENC_CIPHER_AES_128_CTR, key.data(),
                                        static_cast<AP4_Size>(key.size()), 0, 0, nullptr, false,
                                        m_singleSampleDecrypter);
  SetParentIsOwner(false);
  AddSessionKey(defaultKeyId);

  // Define a session id
  m_sessionId = "ck_" + std::to_string(g_sessionIdCount++);
}

void CClearKeyCencSingleSampleDecrypter::AddSessionKey(const std::vector<uint8_t>& keyId)
{
  if (std::find(m_keyIds.begin(), m_keyIds.end(), keyId) == m_keyIds.end())
    m_keyIds.emplace_back(keyId);
}

bool CClearKeyCencSingleSampleDecrypter::HasKeyId(const std::vector<uint8_t>& keyid)
{
  if (!keyid.empty())
  {
    for (const std::vector<uint8_t>& key : m_keyIds)
    {
      if (key == keyid)
        return true;
    }
  }
  return false;
}

AP4_Result CClearKeyCencSingleSampleDecrypter::DecryptSampleData(
    AP4_UI32 pool_id,
    AP4_DataBuffer& data_in,
    AP4_DataBuffer& data_out,
    const AP4_UI08* iv,
    unsigned int subsample_count,
    const AP4_UI16* bytes_of_cleartext_data,
    const AP4_UI32* bytes_of_encrypted_data)
{
  if (!m_singleSampleDecrypter)
  {
    return AP4_FAILURE;
  }
  return (m_singleSampleDecrypter)
      ->DecryptSampleData(data_in, data_out, iv, subsample_count, bytes_of_cleartext_data,
                          bytes_of_encrypted_data);
}

std::string CClearKeyCencSingleSampleDecrypter::CreateLicenseRequest(
    const std::vector<uint8_t>& defaultKeyId)
{
  // github.com/Dash-Industry-Forum/ClearKey-Content-Protection/blob/master/README.md
  /* Expected JSON structure for license request:
   * { "kids":
   *     [
   *         "nrQFDeRLSAKTLifXUIPiZg"
   *     ]
   * "type":"temporary" }
   */

  std::string b64Kid = BASE64::UrlSafeEncode(BASE64::Encode(defaultKeyId, false));

  rapidjson::Document jDoc;
  jDoc.SetObject();
  auto& allocator = jDoc.GetAllocator();

  rapidjson::Value kids{rapidjson::kArrayType};
  rapidjson::Value jKid;

  jKid.SetString(b64Kid.c_str(), allocator);
  kids.PushBack(jKid, allocator);

  jDoc.AddMember("kids", kids, allocator);
  jDoc.AddMember("type", "temporary", allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer{buffer};
  jDoc.Accept(writer);
  return buffer.GetString();
}

bool CClearKeyCencSingleSampleDecrypter::ParseLicenseResponse(std::string data)
{
  /* Expected JSON structure for license response:
   * { "keys": [
   *     {
   *         "k": "FmY0xnWCPCNaSpRG-tUuTQ",
   *         "kid": "nrQFDeRLSAKTLifXUIPiZg",
   *         "kty": "oct"
   *     }
   * "type": "temporary"}
   */

  rapidjson::Document jDoc;
  jDoc.Parse(data.c_str(), data.size());

  if (!jDoc.IsObject())
  {
    LOG::LogF(LOGERROR, "Malformed JSON data in license response");
    return false;
  }

  for (auto& jChildObj : jDoc.GetObject())
  {
    std::string b64Key;
    std::string b64KeyId;
    const std::string keyName = jChildObj.name.GetString();
    rapidjson::Value& jDictVal = jChildObj.value;

    if (keyName == "Message" && jDictVal.IsString())
    {
      LOG::LogF(LOGERROR, "Error in license response: %s", jDictVal.GetString());
      return false;
    }

    if (!jDoc.HasMember("keys"))
    {
      LOG::LogF(LOGERROR, "No keys in license response");
      return false;
    }

    if (keyName == "keys" && jDictVal.IsArray())
    {
      for (auto const& jArrayKey : jDictVal.GetArray())
      {
        if (jArrayKey.IsObject())
        {
          if (jArrayKey.HasMember("k") && jArrayKey["k"].IsString())
            b64Key = jArrayKey["k"].GetString();

          if (jArrayKey.HasMember("kid") && jArrayKey["kid"].IsString())
            b64KeyId = jArrayKey["kid"].GetString();
        }

        if (!b64Key.empty() && !b64KeyId.empty())
        {
          b64Key = BASE64::UrlSafeDecode(b64Key);
          BASE64::AddPadding(b64Key);

          b64KeyId = BASE64::UrlSafeDecode(b64KeyId);
          BASE64::AddPadding(b64KeyId);

          m_keyPairs.emplace(b64KeyId, b64Key);
        }
      }
    }
  }
  return true;
}
