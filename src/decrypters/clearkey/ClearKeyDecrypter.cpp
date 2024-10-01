/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ClearKeyDecrypter.h"

#include "ClearKeyCencSingleSampleDecrypter.h"
#include "decrypters/Helpers.h"
#include "utils/log.h"

std::vector<std::string_view> CClearKeyDecrypter::SelectKeySystems(std::string_view keySystem)
{
  std::vector<std::string_view> keySystems;
  if (keySystem == KS_CLEARKEY)
  {
    keySystems.emplace_back(URN_CLEARKEY);
    keySystems.emplace_back(URN_COMMON);
  }
  return keySystems;
}

bool CClearKeyDecrypter::OpenDRMSystem(const DRM::Config& config)
{
  m_config = config;
  m_isInitialized = true;
  return true;
}

std::shared_ptr<Adaptive_CencSingleSampleDecrypter> CClearKeyDecrypter::CreateSingleSampleDecrypter(
    std::vector<uint8_t>& initData,
    const std::vector<uint8_t>& defaultkeyid,
    std::string_view licenseUrl,
    bool skipSessionMessage,
    CryptoMode cryptoMode)
{
  if (cryptoMode != CryptoMode::AES_CTR)
  {
    LOG::LogF(LOGERROR, "Cannot initialize ClearKey DRM. Only \"cenc\" encryption supported.");
    return nullptr;
  }

  std::shared_ptr<CClearKeyCencSingleSampleDecrypter> decrypter;
  const DRM::Config::License& licConfig = m_config.license;

  // If keys / license url are provided by Kodi property, those of the manifest will be overwritten

  if (!licConfig.serverUrl.empty())
    licenseUrl = licConfig.serverUrl;

  if ((!licConfig.keys.empty() || !initData.empty()) && licConfig.serverUrl.empty()) // Keys provided from manifest or Kodi property
  {
    decrypter = std::make_shared<CClearKeyCencSingleSampleDecrypter>(initData, defaultkeyid,
                                                                     licConfig.keys, this);
  }
  else // Clearkey license server URL provided
  {
    decrypter = std::make_shared<CClearKeyCencSingleSampleDecrypter>(licenseUrl, licConfig.reqHeaders,
                                                                     defaultkeyid, this);
  }

  if (!decrypter->HasKeys())
  {
    return nullptr;
  }
  return decrypter;
}

bool CClearKeyDecrypter::HasLicenseKey(
    std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
    const std::vector<uint8_t>& keyId)
{
  if (decrypter)
  {
    auto clearKeyDecrypter =
        std::dynamic_pointer_cast<CClearKeyCencSingleSampleDecrypter>(decrypter);

    if (clearKeyDecrypter)
      return clearKeyDecrypter->HasKeyId(keyId);
    else
      LOG::LogF(LOGFATAL, "Cannot cast the decrypter shared pointer.");
  }
  return false;
}
