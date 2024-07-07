/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ClearKeyDecrypter.h"

#include "ClearKeyCencSingleSampleDecrypter.h"
#include "CompKodiProps.h"
#include "SrvBroker.h"
#include "decrypters/Helpers.h"

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

bool CClearKeyDecrypter::OpenDRMSystem(std::string_view licenseURL,
                                       const std::vector<uint8_t>& serverCertificate,
                                       const uint8_t config)
{
  return true;
}

Adaptive_CencSingleSampleDecrypter* CClearKeyDecrypter::CreateSingleSampleDecrypter(
    std::vector<uint8_t>& pssh,
    std::string_view optionalKeyParameter,
    std::string_view defaultkeyid,
    bool skipSessionMessage,
    CryptoMode cryptoMode)
{
  CClearKeyCencSingleSampleDecrypter* decrypter = nullptr;
  auto& keys = CSrvBroker::GetKodiProps().GetDrmConfig(std::string(DRM::KS_CLEARKEY)).m_keys;
  std::string_view licenseUrl(reinterpret_cast<char*>(pssh.data()), pssh.size());
  if (!keys.empty() || licenseUrl.substr(0, 4) != "http") // keys provided in props or directly in playlist (HLS)
  {
    decrypter = new CClearKeyCencSingleSampleDecrypter(pssh, defaultkeyid, keys, this);
  }
  else // Clearkey license server URL provided
  {
    decrypter = new CClearKeyCencSingleSampleDecrypter(licenseUrl, defaultkeyid, this);
  }
  if (!decrypter->HasKeys())
  {
    delete decrypter;
    decrypter = nullptr;
  }
  return decrypter;
}

void CClearKeyDecrypter::DestroySingleSampleDecrypter(Adaptive_CencSingleSampleDecrypter* decrypter)
{
  if (decrypter)
  {
    delete static_cast<CClearKeyCencSingleSampleDecrypter*>(decrypter);
  }
}

bool CClearKeyDecrypter::HasLicenseKey(Adaptive_CencSingleSampleDecrypter* decrypter,
                                       std::string_view keyId)
{
  if (decrypter)
    return static_cast<CClearKeyCencSingleSampleDecrypter*>(decrypter)->HasKeyId(keyId);
  return false;
}
