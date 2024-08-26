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
#include "utils/log.h"

#include <algorithm>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

using namespace UTILS;

namespace
{
void CkB64Encode(std::string& str)
{
  STRING::ReplaceAll(str, "+", "-");
  STRING::ReplaceAll(str, "/", "_");
}

void CkB64Decode(std::string& str)
{
  STRING::ReplaceAll(str, "-", "+");
  STRING::ReplaceAll(str, "_", "/");
}
}

CClearKeyCencSingleSampleDecrypter::CClearKeyCencSingleSampleDecrypter(
    std::string_view licenseUrl,
    const std::map<std::string, std::string>& licenseHeaders,
    const std::vector<uint8_t>& defaultKeyId,
    CClearKeyDecrypter* host)
  : m_host(host)
{
  if (licenseUrl.empty())
  {
    LOG::LogF(LOGERROR, "License server URL not found");
    return;
  }

  const std::string postData = CreateLicenseRequest(defaultKeyId);

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    const std::string debugFilePath =
        FILESYS::PathCombine(m_host->GetLibraryPath(), "ClearKey.init");
    FILESYS::SaveFile(debugFilePath, postData.c_str(), true);
  }

  CURL::CUrl curl{licenseUrl, postData};
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

  if (!ParseLicenseResponse(response))
  {
    LOG::LogF(LOGERROR, "Could not parse the license server response");
    return;
  }

  const std::string b64DefaultKeyId = BASE64::Encode(defaultKeyId);
  if (!STRING::KeyExists(m_keyPairs, b64DefaultKeyId))
  {
    LOG::LogF(LOGERROR, "Key not found on license server response");
    return;
  }

  const std::vector<uint8_t> keyBytes = BASE64::Decode(m_keyPairs[b64DefaultKeyId]);
  if (AP4_FAILED(AP4_CencSingleSampleDecrypter::Create(AP4_CENC_CIPHER_AES_128_CTR, keyBytes.data(),
                                                       static_cast<AP4_Size>(keyBytes.size()), 0, 0,
                                                       nullptr, false, m_singleSampleDecrypter)))
  {
    LOG::LogF(LOGERROR, "Failed to create AP4_CencSingleSampleDecrypter");
  }
  SetParentIsOwner(false);
  AddSessionKey(defaultKeyId);
}

CClearKeyCencSingleSampleDecrypter::CClearKeyCencSingleSampleDecrypter(
    const std::vector<uint8_t>& initData,
    const std::vector<uint8_t>& defaultKeyId,
    const std::map<std::string, std::string>& keys,
    CClearKeyDecrypter* host)
  : m_host(host)
{
  std::vector<uint8_t> hexKey;

  if (keys.empty()) // Assume key is provided from the manifest
  {
    hexKey = initData;
  }
  else // Key provided in Kodi props
  {
    const std::string hexDefKid = STRING::ToHexadecimal(defaultKeyId);

    if (STRING::KeyExists(keys, hexDefKid))
      STRING::ToHexBytes(keys.at(hexDefKid), hexKey);
    else
      LOG::LogF(LOGERROR, "Missing KeyId \"%s\" on DRM configuration", defaultKeyId.data());
  }

  AP4_CencSingleSampleDecrypter::Create(AP4_CENC_CIPHER_AES_128_CTR, hexKey.data(),
                                        static_cast<AP4_Size>(hexKey.size()), 0, 0, nullptr, false,
                                        m_singleSampleDecrypter);
  SetParentIsOwner(false);
  AddSessionKey(defaultKeyId);
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

  std::string b64Kid = BASE64::Encode(defaultKeyId, false);
  CkB64Encode(b64Kid);

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
      // NOTE: for now we assume that one key only is requested and one only will come in the license response
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
          CkB64Decode(b64Key);
          BASE64::AddPadding(b64Key);

          CkB64Decode(b64KeyId);
          BASE64::AddPadding(b64KeyId);

          m_keyPairs.emplace(b64KeyId, b64Key);
          break;
        }
      }
    }
  }
  return true;
}
