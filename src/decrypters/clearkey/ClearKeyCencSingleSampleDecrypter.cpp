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
#include "decrypters/Helpers.h"
#include "utils/Base64Utils.h"
#include "utils/CurlUtils.h"
#include "utils/FileUtils.h"
#include "utils/GUIUtils.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/log.h"

#include <nlohmann/json.hpp>

#include <algorithm>

using njson = nlohmann::json;
using namespace UTILS;

uint32_t CClearKeyCencSingleSampleDecrypter::g_sessionIdCount = 1;

CClearKeyCencSingleSampleDecrypter::CClearKeyCencSingleSampleDecrypter(
    const std::vector<uint8_t>& defaultKeyId,
    CClearKeyDecrypter* host)
  : m_host(host), m_defaultKeyId(defaultKeyId)
{
}

void CClearKeyCencSingleSampleDecrypter::InitDecrypter()
{
  SetParentIsOwner(false);

  // Define a session id
  m_sessionId = "ck_" + std::to_string(g_sessionIdCount++);
}

SResult CClearKeyCencSingleSampleDecrypter::CreateSession(const std::vector<uint8_t>& pssh,
                                                          std::string_view licenseUrl,
                                                          bool skipSessionMessage)
{
  // NOTE: dont look at m_config.license configuration, since CDRMEngine::ConfigureClearKey takes care of that
  const DRM::Config::License& licConfig = m_config.license;

  if (pssh.empty())
  {
    // Assume that the license URI is provided by manifest or Kodi properties (props can be with keys or URL)
    if (licenseUrl.empty())
    {
      LOG::LogF(LOGERROR, "Cannot decrypt, the license server URI is missing");
      return SResult::Error(GUI::GetLocalizedString(30306));
    }

    // Make license request to get KID/KEY pairs
    std::vector<uint8_t> licenseData;

    // Check if provided license data in URI format, otherwise make the license request
    if (!URL::GetUriByteData(licenseUrl, licenseData))
    {
      if (!MakeLicenseRequest(std::string(licenseUrl), licConfig.reqHeaders, m_defaultKeyId,
                              licenseData))
      {
        return SResult::Error(GUI::GetLocalizedString(30303));
      }
    }

    if (!ParseLicenseResponse(licenseData))
    {
      LOG::LogF(LOGERROR, "Could not parse the license data");
      return SResult::Error(GUI::GetLocalizedString(30303));
    }

    if (!STRING::KeyExists(m_kidPairs, m_defaultKeyId))
    {
      LOG::LogF(LOGERROR, "License data does not have the required KID");
      m_kidPairs.clear();
      return SResult::Error(GUI::GetLocalizedString(30303));
    }
  }
  else // Keys should be provided by the manifest
  {
    if (DRM::IsValidPsshHeader(pssh))
    {
      LOG::LogF(LOGERROR, "Unable to determine the key using the provided PSSH data");
      return SResult::Error(GUI::GetLocalizedString(30303));
    }
    // Currently HLS manifest only support this
    // the initData should contain only the key
    m_kidPairs.emplace(m_defaultKeyId, pssh);
  }

  InitDecrypter();

  return HasKeys() ? SResult::Ok() : SResult::Error(GUI::GetLocalizedString(30303));
}

bool CClearKeyCencSingleSampleDecrypter::HasKeyId(const std::vector<uint8_t>& keyid)
{
  return STRING::KeyExists(m_kidPairs, keyid);
}

AP4_UI32 CClearKeyCencSingleSampleDecrypter::AddPool()
{
  AP4_UI32 poolId;
  do
  {
    poolId = m_nextPoolId++;
  } while (!m_pool.emplace(poolId, PINFO()).second);

  return poolId;
}

void CClearKeyCencSingleSampleDecrypter::RemovePool(AP4_UI32 poolId)
{
  m_pool.erase(poolId);
}

AP4_Result CClearKeyCencSingleSampleDecrypter::SetFragmentInfo(
    AP4_UI32 poolId,
    const std::vector<uint8_t>& keyId,
    const AP4_UI08 nalLengthSize,
    const std::vector<uint8_t>& annexbSpsPps,
    DRM::Capabilities flags,
    CryptoInfo cryptoInfo)
{
  if (!STRING::KeyExists(m_pool, poolId))
  {
    LOG::LogF(LOGERROR, "Cannot set fragment info, the pool id %u dont exist", poolId);
    return AP4_ERROR_INVALID_PARAMETERS;
  }

  if (cryptoInfo.m_mode != CryptoMode::NONE && cryptoInfo.m_mode != CryptoMode::AES_CTR &&
      cryptoInfo.m_mode != CryptoMode::AES_CBC)
  {
    LOG::LogF(LOGERROR, "Cannot set fragment info, unsupported crypto mode (%i)",
              static_cast<int>(cryptoInfo.m_mode));
    return AP4_ERROR_INVALID_PARAMETERS;
  }

  PINFO& pInfo = m_pool[poolId];
  FINFO& fInfo = pInfo.fInfo;

  // Compare the encryption info from the previous fragment to see if it has been changed,
  // if so, the decrypter will have to be recreated
  pInfo.isChanged = fInfo.kid != keyId || fInfo.cryptoInfo.m_mode != cryptoInfo.m_mode ||
                    fInfo.cryptoInfo.m_cryptBlocks != cryptoInfo.m_cryptBlocks ||
                    fInfo.cryptoInfo.m_skipBlocks != cryptoInfo.m_skipBlocks;

  // Update with the current fragment info
  fInfo.kid = keyId;
  fInfo.cryptoInfo = cryptoInfo;

  return AP4_SUCCESS;
}

AP4_Result CClearKeyCencSingleSampleDecrypter::DecryptSampleData(
    AP4_UI32 poolId,
    AP4_DataBuffer& dataIn,
    AP4_DataBuffer& dataOut,
    const AP4_UI08* iv,
    unsigned int subsampleCount,
    const AP4_UI16* bytesOfCleartextData,
    const AP4_UI32* bytesOfEncryptedData,
    DRM::DRMMediaType streamType)
{
  if (m_pool.empty())
  {
    LOG::LogF(LOGERROR, "Cannot decrypt data, the pool is empty");
    return AP4_ERROR_INTERNAL;
  }
  if (!STRING::KeyExists(m_pool, poolId))
  {
    LOG::LogF(LOGERROR, "Cannot decrypt data, the pool id %u dont exist", poolId);
    return AP4_ERROR_INVALID_PARAMETERS;
  }

  PINFO& pInfo = m_pool[poolId];
  auto& decrypter = pInfo.decrypter;

  if (!decrypter || pInfo.isChanged)
  {
    const auto& fInfo = pInfo.fInfo;

    if (!STRING::KeyExists(m_kidPairs, fInfo.kid))
    {
      // In theory when there is a license server url, could be possible to request new KID/KEY pair
      // making a new HTTP license request e.g. the case of key rotation, but it needs to be tested properly
      LOG::LogF(LOGERROR, "Cannot decrypt data, due to missing KID/KEY from the license data");
      return AP4_ERROR_INVALID_STATE;
    }

    AP4_CencSingleSampleDecrypter* pDecrypter{nullptr};
    const std::vector<uint8_t>& key = m_kidPairs[fInfo.kid];

    AP4_UI32 cypherType{AP4_CENC_CIPHER_NONE};
    bool resetIvEachSubsample{false};

    if (fInfo.cryptoInfo.m_mode == CryptoMode::AES_CTR)
    {
      cypherType = AP4_CENC_CIPHER_AES_128_CTR;
    }
    else if (fInfo.cryptoInfo.m_mode == CryptoMode::AES_CBC)
    {
      cypherType = AP4_CENC_CIPHER_AES_128_CBC;
      // CBCS reset the IV at each subsample, see https://github.com/axiomatic-systems/Bento4/commit/ab07e3acc7befc821554bc5e271df1201681e954
      resetIvEachSubsample = true;
    }

    AP4_CencSingleSampleDecrypter::Create(
        cypherType, key.data(), static_cast<AP4_Size>(key.size()), fInfo.cryptoInfo.m_cryptBlocks,
        fInfo.cryptoInfo.m_skipBlocks, nullptr, resetIvEachSubsample, pDecrypter);

    decrypter = std::unique_ptr<AP4_CencSingleSampleDecrypter>{std::exchange(pDecrypter, nullptr)};
  }

  return decrypter->DecryptSampleData(dataIn, dataOut, iv, subsampleCount, bytesOfCleartextData,
                                      bytesOfEncryptedData);
}

std::string CClearKeyCencSingleSampleDecrypter::CreateLicenseRequest(
    const std::vector<uint8_t>& defaultKeyId)
{
  // github.com/Dash-Industry-Forum/ClearKey-Content-Protection/blob/master/README.md
  /* Expected JSON structure for license request:
   * { "kids":
   *     [
   *         "nrQFDeRLSAKTLifXUIPiZg"
   *     ],
   * "type":"temporary" }
   */

  std::string b64Kid = BASE64::UrlSafeEncode(BASE64::Encode(defaultKeyId, false));

  njson jData;
  jData["kids"] = njson::array({b64Kid});
  jData["type"] = "temporary";
  return jData.dump(-1, ' ', false, njson::error_handler_t::ignore);
}

bool CClearKeyCencSingleSampleDecrypter::MakeLicenseRequest(
    const std::string& url,
    const std::map<std::string, std::string>& headers,
    const std::vector<uint8_t>& kid,
    std::vector<uint8_t>& licenseData)
{
  const std::string postData = CreateLicenseRequest(kid);

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    const std::string debugFilePath =
        FILESYS::PathCombine(m_host->GetLibraryPath(), "ClearKey.init");
    FILESYS::SaveFile(debugFilePath, postData.c_str(), true);
  }

  CURL::CUrl curl{url, postData};
  curl.AddHeader("Accept", "application/json");
  curl.AddHeader("Content-Type", "application/json");
  curl.AddHeaders(headers);

  int statusCode = curl.Open();
  if (statusCode == -1 || statusCode >= 400)
  {
    LOG::Log(LOGERROR, "License server returned failure (HTTP error %i)", statusCode);
    return false;
  }

  std::string responseData;

  if (curl.Read(responseData) != CURL::ReadStatus::IS_EOF)
  {
    LOG::LogF(LOGERROR, "Could not read the license server response");
    return false;
  }

  if (CSrvBroker::GetSettings().IsDebugLicense())
  {
    const std::string debugFilePath =
        FILESYS::PathCombine(m_host->GetLibraryPath(), "ClearKey.response");
    FILESYS::SaveFile(debugFilePath, responseData, true);
  }

  licenseData.assign(responseData.begin(), responseData.end());

  return true;
}

bool CClearKeyCencSingleSampleDecrypter::ParseLicenseResponse(const std::vector<uint8_t>& data)
{
  /* Expected JSON structure for license response:
   * { "keys": [
   *     {
   *         "k": "FmY0xnWCPCNaSpRG-tUuTQ",
   *         "kid": "nrQFDeRLSAKTLifXUIPiZg",
   *         "kty": "oct"
   *     }],
   * "type": "temporary"}
   */
  const njson jData = njson::parse(data, nullptr, false);
  if (jData.is_discarded() || !jData.is_object())
  {
    LOG::LogF(LOGERROR, "Malformed JSON data in license response");
    return false;
  }

  if (jData.contains("Message") && jData["Message"].is_string())
  {
    LOG::LogF(LOGERROR, "Error in license response: %s",
              jData["Message"].get<std::string>().c_str());
    return false;
  }
  if (!jData.contains("keys") || !jData["keys"].is_array() || jData["keys"].empty())
  {
    LOG::LogF(LOGERROR, "No keys in license response");
    return false;
  }

  for (const auto& jwkValue : jData["keys"]) // Iterate array
  {
    if (!jwkValue.is_object())
    {
      LOG::LogF(LOGERROR, "Unexpected JSON format in the license response");
      continue;
    }

    if (jwkValue.contains("kty") && jwkValue["kty"].is_string() &&
        jwkValue["kty"].get<std::string>() != "oct")
    {
      LOG::LogF(LOGWARNING, "Ignored JWK set, due to unsupported \"%s\" key type",
                jwkValue["kty"].get<std::string>().c_str());
      continue;
    }

    std::string b64Key;
    std::string b64KeyId;

    if (jwkValue.contains("k") && jwkValue["k"].is_string())
      b64Key = jwkValue["k"].get<std::string>();

    if (jwkValue.contains("kid") && jwkValue["kid"].is_string())
      b64KeyId = jwkValue["kid"].get<std::string>();

    b64Key = BASE64::UrlSafeDecode(b64Key);
    BASE64::AddPadding(b64Key);

    b64KeyId = BASE64::UrlSafeDecode(b64KeyId);
    BASE64::AddPadding(b64KeyId);

    const std::vector<uint8_t> kidBytes = BASE64::Decode(b64KeyId);
    const std::vector<uint8_t> keyBytes = BASE64::Decode(b64Key);

    if (kidBytes.empty() || keyBytes.empty())
    {
      LOG::LogF(LOGERROR, "Malformed key pair value in the license response");
      continue;
    }

    m_kidPairs.insert_or_assign(kidBytes, keyBytes);
  }

  return true;
}
