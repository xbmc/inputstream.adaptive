/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "common/AdaptiveCencSampleDecrypter.h"
#include "decrypters/IDecrypter.h"

#include <map>

class CClearKeyDecrypter;

class CClearKeyCencSingleSampleDecrypter : public Adaptive_CencSingleSampleDecrypter
{
public:
  CClearKeyCencSingleSampleDecrypter(std::string_view licenseUrl,
                                     const std::map<std::string, std::string>& licenseHeaders,
                                     const std::vector<uint8_t>& defaultKeyId,
                                     CClearKeyDecrypter* host);
  CClearKeyCencSingleSampleDecrypter(const std::vector<uint8_t>& initdata,
                                     const std::vector<uint8_t>& defaultKeyId,
                                     const std::map<std::string, std::string>& keys,
                                     CClearKeyDecrypter* host);
  virtual ~CClearKeyCencSingleSampleDecrypter(){};
  void AddSessionKey(const std::vector<uint8_t>& keyId);
  bool HasKeyId(const std::vector<uint8_t>& keyid);
  virtual AP4_Result SetFragmentInfo(AP4_UI32 pool_id,
                                     const std::vector<uint8_t>& key,
                                     const AP4_UI08 nal_length_size,
                                     AP4_DataBuffer& annexb_sps_pps,
                                     AP4_UI32 flags,
                                     CryptoInfo cryptoInfo) override
  {
    return AP4_SUCCESS;
  }
  virtual AP4_Result DecryptSampleData(AP4_UI32 pool_id,
                                       AP4_DataBuffer& data_in,
                                       AP4_DataBuffer& data_out,
                                       const AP4_UI08* iv,
                                       unsigned int subsample_count,
                                       const AP4_UI16* bytes_of_cleartext_data,
                                       const AP4_UI32* bytes_of_encrypted_data) override;
  std::string CreateLicenseRequest(const std::vector<uint8_t>& defaultKeyId);
  bool ParseLicenseResponse(std::string data);
  void SetDefaultKeyId(const std::vector<uint8_t>& keyId) override{};
  void AddKeyId(const std::vector<uint8_t>& keyId) override{};
  bool HasKeys() { return !m_keyIds.empty(); }

private:
  AP4_CencSingleSampleDecrypter* m_singleSampleDecrypter{nullptr};
  std::string m_strSession;
  std::string m_licenceDefaultKeyId;
  std::vector<std::vector<uint8_t>> m_keyIds;
  std::map<std::string, std::string> m_keyPairs;
  CClearKeyDecrypter* m_host;
};
