/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Base64Utils.h"

#include "log.h"

#include <regex>

using namespace UTILS::BASE64;

namespace
{
constexpr char PADDING{'='};
constexpr std::string_view CHARACTERS{"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                      "abcdefghijklmnopqrstuvwxyz"
                                      "0123456789+/"};
// clang-format off
constexpr unsigned char BASE64_TABLE[] = {
    255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255,
    255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255,
    255,255,255,255, 255,255,255,255, 255,255,255, 62, 255,255,255, 63,
     52, 53, 54, 55,  56, 57, 58, 59,  60, 61,255,255, 255,  0,255,255, /* Note PAD->0 */
    255,  0,  1,  2,   3,  4,  5,  6,   7,  8,  9, 10,  11, 12, 13, 14,
     15, 16, 17, 18,  19, 20, 21, 22,  23, 24, 25,255, 255,255,255,255,
    255, 26, 27, 28,  29, 30, 31, 32,  33, 34, 35, 36,  37, 38, 39, 40,
     41, 42, 43, 44,  45, 46, 47, 48,  49, 50, 51,255, 255,255,255,255,

    255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255,
    255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255,
    255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255,
    255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255,
    255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255,
    255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255,
    255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255,
    255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255,
};
// clang-format on
} // namespace

void UTILS::BASE64::Encode(const uint8_t* input,
                           const size_t length,
                           std::string& output,
                           const bool padding /* = true */)
{
  if (input == nullptr || length == 0)
    return;

  long l;
  output.clear();
  output.reserve(((length + 2) / 3) * 4);

  for (size_t i{0}; i < length; i += 3)
  {
    l = (((static_cast<unsigned long>(input[i])) << 16) & 0xFFFFFF) |
        ((((i + 1) < length) ? ((static_cast<unsigned long>(input[i + 1])) << 8) : 0) & 0xFFFF) |
        ((((i + 2) < length) ? ((static_cast<unsigned long>(input[i + 2])) << 0) : 0) & 0x00FF);

    output.push_back(CHARACTERS[(l >> 18) & 0x3F]);
    output.push_back(CHARACTERS[(l >> 12) & 0x3F]);

    if (i + 1 < length)
      output.push_back(CHARACTERS[(l >> 6) & 0x3F]);
    if (i + 2 < length)
      output.push_back(CHARACTERS[(l >> 0) & 0x3F]);
  }

  if (padding)
  {
    const int left = 3 - (length % 3);

    if (length % 3)
    {
      for (int i = 0; i < left; ++i)
        output.push_back(PADDING);
    }
  }
}

std::string UTILS::BASE64::Encode(const uint8_t* input,
                                  const size_t length,
                                  const bool padding /* = true */)
{
  std::string output;
  Encode(input, length, output, padding);
  return output;
}

std::string UTILS::BASE64::Encode(const std::vector<uint8_t>& input, const bool padding /* = true */)
{
  std::string output;
  Encode(input.data(), input.size(), output, padding);
  return output;
}

std::string UTILS::BASE64::Encode(const std::vector<char>& input, const bool padding /* = true */)
{
  std::string output;
  Encode(reinterpret_cast<const uint8_t*>(input.data()), input.size(), output, padding);
  return output;
}

std::string UTILS::BASE64::Encode(const std::string& inputStr, const bool padding /* = true */)
{
  std::string output;
  Encode(reinterpret_cast<const uint8_t*>(inputStr.data()), inputStr.size(), output, padding);
  return output;
}

void UTILS::BASE64::Decode(const char* input, const size_t length, std::vector<uint8_t>& output)
{
  if (!input)
    return;

  output.clear();
  output.reserve(length - ((length + 2) / 4));

  bool paddingStarted{false};
  int quadPos{0};
  unsigned char leftChar{0};
  int pads{0};

  for (size_t i{0}; i < length; i++)
  {
    // Check for pad sequences and ignore the invalid ones.
    if (input[i] == PADDING)
    {
      paddingStarted = true;

      if (quadPos >= 2 && quadPos + ++pads >= 4)
      {
        // A pad sequence means we should not parse more input.
        // We've already interpreted the data from the quad at this point.
        return;
      }
      continue;
    }

    unsigned char thisChar{BASE64_TABLE[static_cast<unsigned char>(input[i])]};
    // Skip not allowed characters
    if (thisChar >= 64)
      continue;

    // Characters that are not '=', in the middle of the padding, are not allowed
    if (paddingStarted)
    {
      LOG::LogF(LOGERROR, "Invalid base64-encoded string: Incorrect padding characters");
      output.clear();
      return;
    }
    pads = 0;

    switch (quadPos)
    {
      case 0:
        quadPos = 1;
        leftChar = thisChar;
        break;
      case 1:
        quadPos = 2;
        output.push_back((leftChar << 2) | (thisChar >> 4));
        leftChar = thisChar & 0x0f;
        break;
      case 2:
        quadPos = 3;
        output.push_back((leftChar << 4) | (thisChar >> 2));
        leftChar = thisChar & 0x03;
        break;
      case 3:
        quadPos = 0;
        output.push_back((leftChar << 6) | (thisChar));
        leftChar = 0;
        break;
    }
  }

  if (quadPos != 0)
  {
    if (quadPos == 1)
    {
      // There is exactly one extra valid, non-padding, base64 character.
      // This is an invalid length, as there is no possible input that
      // could encoded into such a base64 string.
      LOG::LogF(LOGERROR, "Invalid base64-encoded string: number of data characters cannot be 1 "
                          "more than a multiple of 4");
    }
    else
    {
      LOG::LogF(LOGERROR, "Invalid base64-encoded string: Incorrect padding");
    }
    output.clear();
  }
}

std::vector<uint8_t> UTILS::BASE64::Decode(std::string_view input)
{
  std::vector<uint8_t> data;
  Decode(input.data(), input.size(), data);
  return data;
}

std::string UTILS::BASE64::DecodeToStr(std::string_view input)
{
  std::vector<uint8_t> output;
  Decode(input.data(), input.size(), output);
  return {output.begin(), output.end()};
}

bool UTILS::BASE64::IsValidBase64(const std::string& input)
{
  // Check for empty input or incorrect length
  if (input.empty() || input.size() % 4 != 0)
    return false;

  // Use a lookup table for faster character checking
  bool lookup[256]{};
  for (char c : CHARACTERS)
  {
    lookup[static_cast<unsigned char>(c)] = true;
  }

  // Iterate over the input string and check each character
  size_t paddingSize = 0;
  for (size_t i = 0; i < input.size(); ++i)
  {
    if (input[i] == '=')
    {
      paddingSize++;
    } // Check of characters after padding, and validity of characters
    else if (paddingSize > 0 || !lookup[static_cast<unsigned char>(input[i])])
    {
      return false; // Invalid character
    }
  }

  // Max allowed padding chars
  if (paddingSize > 2)
    return false;

  return true;
}

bool UTILS::BASE64::AddPadding(std::string& base64str)
{
  const int mod = static_cast<int>(base64str.length() % 4);
  if (mod > 0)
  {
    for (int i = 4 - mod; i > 0; --i)
    {
      base64str.push_back(PADDING);
    }
    return true;
  }
  return false;
}
