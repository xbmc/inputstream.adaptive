/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace DRM
{
// DRM Key systems

constexpr std::string_view KS_NONE = "none"; // No DRM but however encrypted (e.g. AES-128 on HLS)
constexpr std::string_view KS_WIDEVINE = "com.widevine.alpha";
constexpr std::string_view KS_PLAYREADY = "com.microsoft.playready";
constexpr std::string_view KS_WISEPLAY = "com.huawei.wiseplay";
constexpr std::string_view KS_CLEARKEY = "org.w3.clearkey";

// DRM UUIDs

constexpr std::string_view URN_WIDEVINE = "urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed";
constexpr std::string_view URN_PLAYREADY = "urn:uuid:9a04f079-9840-4286-ab92-e65be0885f95";
constexpr std::string_view URN_WISEPLAY = "urn:uuid:3d5e6d35-9b9a-41e8-b843-dd3c6e72c42c";
constexpr std::string_view URN_CLEARKEY = "urn:uuid:e2719d58-a985-b3c9-781a-b030af78d30e";
constexpr std::string_view URN_COMMON = "urn:uuid:1077efec-c0b2-4d02-ace3-3c1e52e2fb4b";

// DRM System ID's

constexpr uint8_t ID_WIDEVINE[16] = {0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce,
                                     0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed};
constexpr uint8_t ID_PLAYREADY[16] = {0x9a, 0x04, 0xf0, 0x79, 0x98, 0x40, 0x42, 0x86,
                                      0xab, 0x92, 0xe6, 0x5b, 0xe0, 0x88, 0x5f, 0x95};
constexpr uint8_t ID_WISEPLAY[16] = {0x3d, 0x5e, 0x6d, 0x35, 0x9b, 0x9a, 0x41, 0xe8,
                                     0xb8, 0x43, 0xdd, 0x3c, 0x6e, 0x72, 0xc4, 0x2c};
constexpr uint8_t ID_CLEARKEY[16] = {0xe2, 0x71, 0x9d, 0x58, 0xa9, 0x85, 0xb3, 0xc9,
                                     0x78, 0x1a, 0xb0, 0x30, 0xaf, 0x78, 0xd3, 0x0e};

std::string KeySystemToDrmName(std::string_view ks);

const uint8_t* KeySystemToUUID(std::string_view ks);

bool IsKeySystemSupported(std::string_view keySystem);

/*!
 * \brief Generate an hash by using the base domain of an URL.
 * \param url An URL
 * \return The hash of a base domain URL
 */
std::string GenerateUrlDomainHash(std::string_view url);

/*!
 * \brief Convert DRM URN to System ID.
 * \param urn The URN
 * \return The System ID, otherwise empty if fails.
 */
std::string UrnToSystemId(std::string_view urn);

/*!
 * \brief Convert a list of DRM URN's to System ID's.
 * \param urn The URN
 * \return The System ID's, failed conversions are not included.
 */
std::vector<std::string> UrnsToSystemIds(const std::vector<std::string_view>& urns);

/*!
 * \brief Convert a hexdecimal KeyId of 32 chars to 16 bytes.
 * \param kidStr The hexdecimal KeyId
 * \return KeyId as bytes, otherwise empty if fails.
 */
std::vector<uint8_t> ConvertKidStrToBytes(std::string_view kidStr);

/*!
 * \brief Convert a KeyId of 16 bytes to a KeyId UUID format.
 * \param kidStr The hexdecimal KeyId
 * \return The KeyId UUID, otherwise empty if fails.
 */
std::string ConvertKidBytesToUUID(std::vector<uint8_t> kid);

bool IsValidPsshHeader(const std::vector<uint8_t>& pssh);

class PSSH
{
public:
 /*!
  * \brief Generate a PSSH box.
  *        https://w3c.github.io/encrypted-media/format-registry/initdata/cenc.html#common-system
  * \param systemId The DRM System ID (expected 16 bytes)
  * \param keyIds The key id's
  * \param initData[OPT] The pssh data e.g. WidevinePsshData
  * \param version[OPT] The pssh box version (0 or 1)
  * \param flags[OPT] The pssh box flags
  * \return The pssh if has success, otherwise empty.
  */
  static std::vector<uint8_t> Make(const uint8_t* systemId,
                                   const std::vector<std::vector<uint8_t>>& keyIds,
                                   const std::vector<uint8_t>& initData = {},
                                   const uint8_t version = 0,
                                   const uint32_t flags = 0);

  /*!
  * \brief Generate a PSSH box for Widevine.
  * \param keyIds The key id's
  * \param initData[OPT] Custom init data for the "content_id" field of WidevinePsshData struct
  * \param version[OPT] The pssh box version (0 or 1)
  * \param flags[OPT] The pssh box flags
  * \return The pssh if has success, otherwise empty.
  */
  static std::vector<uint8_t> MakeWidevine(const std::vector<std::vector<uint8_t>>& keyIds,
                                           const std::vector<uint8_t>& initData = {},
                                           const uint8_t version = 0,
                                           const uint32_t flags = 0);

  /*!
  * \brief Parse a PSSH, and the PSSH init data.
  * \param data The PSSH
  * \return True has success, otherwise false.
  */
  bool Parse(const std::vector<uint8_t>& data);

  uint8_t GetVersion() const { return m_version; }
  uint32_t GetFlags() const { return m_flags; }
  const std::vector<uint8_t>& GetSystemId() const { return m_systemId; }
  const std::vector<std::vector<uint8_t>>& GetKeyIds() const { return m_keyIds; }
  const std::vector<uint8_t>& GetInitData() const { return m_initData; }
  std::string GetLicenseUrl() const { return m_licenseUrl; }

private:
  void ResetData();

  uint8_t m_version{0};
  uint32_t m_flags{0};
  std::vector<uint8_t> m_systemId;
  std::vector<std::vector<uint8_t>> m_keyIds;
  std::vector<uint8_t> m_initData;
  std::string m_licenseUrl;
};

}; // namespace DRM
