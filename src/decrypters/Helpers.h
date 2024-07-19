/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <string>
#include <string_view>

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

}; // namespace DRM
