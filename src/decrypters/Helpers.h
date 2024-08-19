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

std::vector<uint8_t> ConvertKidToUUIDVec(const std::vector<uint8_t>& kid);

/*!
 * \brief Convert a PlayReady KeyId of 16 bytes to a Widevine KeyId.
 * \param kid The PlayReady KeyId
 * \return The Widevine KeyId, otherwise empty if fails.
 */
std::vector<uint8_t> ConvertPrKidtoWvKid(std::vector<uint8_t> kid);

bool IsValidPsshHeader(const std::vector<uint8_t>& pssh);

/*!
 * \brief Generate a synthesized Widevine PSSH.
 *        (WidevinePsshData as google protobuf format
 *        https://github.com/devine-dl/pywidevine/blob/master/pywidevine/license_protocol.proto)
 * \param kid The KeyId
 * \param contentIdData Custom content for the "content_id" field as bytes
 *                      Placeholders allowed:
 *                      {KID} To inject the KID as bytes
 *                      {UUID} To inject the KID as UUID string format
 * \param wvPsshData[OUT] The generated Widevine PSSH
 * \return True if has success, otherwise false.
 */
bool MakeWidevinePsshData(const std::vector<uint8_t>& kid,
                          std::vector<uint8_t> contentIdData,
                          std::vector<uint8_t>& wvPsshData);

/*!
 * \brief Generate a PSSH box (version 0, no KID's).
 * \param systemUuid The DRM System ID (expected 16 bytes)
 * \param initData The init data e.g. WidevinePsshData
 * \param psshData[OUT] The generated PSSH
 * \return True if has success, otherwise false.
 */
bool MakePssh(const uint8_t* systemId,
              const std::vector<uint8_t>& initData,
              std::vector<uint8_t>& psshData);

}; // namespace DRM
