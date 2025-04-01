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

bool CClearKeyDecrypter::OpenDRMSystem(const DRM::Config& config)
{
  m_config = config;
  m_isInitialized = true;
  return true;
}

std::shared_ptr<Adaptive_CencSingleSampleDecrypter> CClearKeyDecrypter::CreateSingleSampleDecrypter(
    const std::vector<uint8_t>& initData,
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

  // NOTE: dont look at m_config.license configuration, since CDRMEngine::ConfigureClearKey takes care of that
  const DRM::Config::License& licConfig = m_config.license;

  if (initData.empty())
  {
    // Assume that the license URI is provided by manifest or Kodi properties (props can be with keys or URL)
    decrypter = std::make_shared<CClearKeyCencSingleSampleDecrypter>(
        licenseUrl, licConfig.reqHeaders, defaultkeyid, this);
  }
  else
  {
    // Keys should be provided by the manifest
    decrypter = std::make_shared<CClearKeyCencSingleSampleDecrypter>(initData, defaultkeyid, this);
  }

  if (!decrypter->HasKeys())
  {
    return nullptr;
  }
  return decrypter;
}

std::optional<bool> CClearKeyDecrypter::HasLicenseKey(
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
