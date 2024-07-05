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
// CDM Key systems

constexpr std::string_view KS_NONE = "none"; // No DRM but however encrypted (e.g. AES-128 on HLS)
constexpr std::string_view KS_WIDEVINE = "com.widevine.alpha";
constexpr std::string_view KS_PLAYREADY = "com.microsoft.playready";
constexpr std::string_view KS_WISEPLAY = "com.huawei.wiseplay";

// CDM UUIDs

constexpr std::string_view UUID_WIDEVINE = "edef8ba9-79d6-4ace-a3c8-27dcd51d21ed";
constexpr std::string_view UUID_PLAYREADY = "9a04f079-9840-4286-ab92-e65be0885f95";
constexpr std::string_view UUID_WISEPLAY = "3d5e6d35-9b9a-41e8-b843-dd3c6e72c42c";
// constexpr std::string_view UUID_COMMON = "1077efec-c0b2-4d02-ace3-3c1e52e2fb4b";


bool IsKeySystemSupported(std::string_view keySystem);

bool WvUnwrapLicense(std::string wrapper,
                     std::string contentType,
                     std::string data,
                     std::string& dataOut,
                     int& hdcpLimit);

/*!
 * \brief Generate an hash by using the base domain of an URL.
 * \param url An URL
 * \return The hash of a base domain URL
 */
std::string GenerateUrlDomainHash(std::string_view url);

}; // namespace DRM
