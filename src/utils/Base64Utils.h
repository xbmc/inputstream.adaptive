/*
 *  Copyright (C) 2022 Team Kodi
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

namespace UTILS
{
namespace BASE64
{

void Encode(const uint8_t* input, const size_t length, std::string& output, const bool padding = true);
std::string Encode(const uint8_t* input, const size_t length, const bool padding = true);
std::string Encode(const std::vector<uint8_t>& input, const bool padding = true);
std::string Encode(const std::vector<char>& input, const bool padding = true);
std::string Encode(const std::string& input, const bool padding = true);

void Decode(const char* input, const size_t length, std::vector<uint8_t>& output);
std::vector<uint8_t> Decode(std::string_view input);
std::string DecodeToStr(std::string_view input);

bool IsValidBase64(const std::string& input);

bool AddPadding(std::string& base64str);

} // namespace BASE64
} // namespace UTILS
