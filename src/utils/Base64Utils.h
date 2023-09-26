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

void Encode(const char* input, const size_t length, std::string& output);
std::string Encode(const unsigned char* input, const size_t length);
std::string Encode(const char* input, const size_t length);
void Encode(const std::string& input, std::string& output);
std::string Encode(const std::string& input);

std::vector<uint8_t> DecodeStrToUint8(const std::string& input);
std::vector<uint8_t> DecodeToUint8(const std::vector<uint8_t>& input);
void DecodeData(const uint8_t* input, const size_t length, std::vector<uint8_t>& output);

void Decode(const char* input, const size_t length, std::string& output);
std::string Decode(const char* input, const size_t length);
void Decode(std::string_view input, std::string& output);
std::string Decode(std::string_view input);

} // namespace BASE64
} // namespace UTILS
