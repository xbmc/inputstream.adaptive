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
                                     std::string_view defaultKeyId,
                                     CClearKeyDecrypter* host);
  CClearKeyCencSingleSampleDecrypter(std::vector<uint8_t>& pssh,
                                     std::string_view defaultKeyId,
                                     std::map<std::string, std::string> keys,
                                     CClearKeyDecrypter* host);
  virtual ~CClearKeyCencSingleSampleDecrypter(){};
  void AddSessionKey(std::string_view keyId);
  bool HasKeyId(std::string_view keyid);
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
  std::string CreateLicenseRequest(std::string_view defaultKeyId);
  bool ParseLicenseResponse(std::string data);
  void SetDefaultKeyId(std::string_view keyId) override{};
  void AddKeyId(std::string_view keyId) override{};
  bool HasKeys() { return !m_keyIds.empty(); }

private:
  AP4_CencSingleSampleDecrypter* m_singleSampleDecrypter{nullptr};
  std::string m_strSession;
  std::string m_licenceDefaultKeyId;
  std::vector<std::string> m_keyIds;
  std::map<std::string, std::string> m_keyPairs;
  CClearKeyDecrypter* m_host;
};
