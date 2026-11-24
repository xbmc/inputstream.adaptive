/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "common/AdaptiveCencSampleDecrypter.h"
#include "decrypters/IDecrypter.h"
#include "utils/CryptoUtils.h"

#include <cstdint>
#include <map>
#include <vector>

class CClearKeyDecrypter;

class CClearKeyCencSingleSampleDecrypter : public Adaptive_CencSingleSampleDecrypter
{
public:
  CClearKeyCencSingleSampleDecrypter(const std::vector<uint8_t>& defaultKeyId,
                                     CClearKeyDecrypter* host);
  virtual ~CClearKeyCencSingleSampleDecrypter(){};

  virtual SResult CreateSession(const std::vector<uint8_t>& pssh,
                                std::string_view licenseUrl,
                                bool skipSessionMessage) override;

  bool HasKeyId(const std::vector<uint8_t>& keyid);
  virtual AP4_UI32 AddPool() override;
  virtual void RemovePool(AP4_UI32 poolId) override;
  virtual AP4_Result SetFragmentInfo(AP4_UI32 poolId,
                                     const std::vector<uint8_t>& keyId,
                                     const AP4_UI08 nalLengthSize,
                                     const std::vector<uint8_t>& annexbSpsPps,
                                     DRM::Capabilities flags,
                                     CryptoInfo cryptoInfo) override;
  virtual AP4_Result DecryptSampleData(AP4_UI32 poolId,
                                       AP4_DataBuffer& dataIn,
                                       AP4_DataBuffer& dataOut,
                                       const AP4_UI08* iv,
                                       unsigned int subsampleCount,
                                       const AP4_UI16* bytesOfCleartextData,
                                       const AP4_UI32* bytesOfEncryptedData,
                                       DRM::DRMMediaType streamType) override;
  void SetDefaultKeyId(const std::vector<uint8_t>& keyId) override{};
  void AddKeyId(const std::vector<uint8_t>& keyId) override{};
  bool HasKeys() { return !m_kidPairs.empty(); }
  std::string GetSessionId() override { return m_sessionId; }

private:
  void InitDecrypter();
  std::string CreateLicenseRequest(const std::vector<uint8_t>& defaultKeyId);
  bool MakeLicenseRequest(const std::string& url,
                          const std::map<std::string, std::string>& headers,
                          const std::vector<uint8_t>& kid,
                          std::vector<uint8_t>& licenseData);
  bool ParseLicenseResponse(const std::vector<uint8_t>& data);

  CClearKeyDecrypter* m_host;

  std::map<std::vector<uint8_t>, std::vector<uint8_t>> m_kidPairs; // KID - KEY pair

  // \brief Fragment info
  struct FINFO
  {
    std::vector<uint8_t> kid;
    CryptoInfo cryptoInfo;
  };

  // \brief Decrypter pool info
  struct PINFO
  {
    FINFO fInfo;
    bool isChanged{false}; // If true, means FINFO has been updated with different encryption info from previous fragment
    std::unique_ptr<AP4_CencSingleSampleDecrypter> decrypter;
  };

  // \brif Decrypter pool
  std::map<AP4_UI32, PINFO> m_pool; // ID - Pool info
  AP4_UI32 m_nextPoolId{0};

  DRM::Config m_config;
  std::vector<uint8_t> m_defaultKeyId;
  std::string m_sessionId;
  static uint32_t g_sessionIdCount;
};
