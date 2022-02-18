/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace UTILS
{

std::string AnnexbToHvcc(const char* b16Data);
std::string AnnexbToAvc(const char* b16Data);
std::string AvcToAnnexb(const std::string& avc);
std::string ConvertKIDtoWVKID(std::string_view kid);
std::string ConvertKIDtoUUID(std::string_view kid);
bool CreateISMlicense(std::string_view key,
                      std::string_view licenseData,
                      std::vector<uint8_t>& initData);
void ParseHeaderString(std::map<std::string, std::string>& headerMap, const std::string& header);

/*!
 * \brief Make a FourCC code as unsigned integer value
 * \param c1 The first FourCC char
 * \param c2 The second FourCC char
 * \param c3 The third FourCC char
 * \param c4 The fourth FourCC char
 * \return The FourCC as unsigned integer value
 */
constexpr uint32_t MakeFourCC(char c1, char c2, char c3, char c4)
{
  return ((static_cast<uint32_t>(c1) << 24) | (static_cast<uint32_t>(c2) << 16) |
    (static_cast<uint32_t>(c3) << 8) | (static_cast<uint32_t>(c4)));
}

} // namespace UTILS
