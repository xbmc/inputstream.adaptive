/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "StringUtils.h"

#include "kodi/tools/StringUtils.h"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <sstream>

using namespace UTILS::STRING;
using namespace kodi::tools;

namespace
{
/*!
 * \brief Converts a string to a number of a specified type, by using istringstream.
 * \param str The string to convert
 * \param fallback [OPT] The number to return when the conversion fails
 * \return The converted number, otherwise fallback if conversion fails
 */
template<typename T>
T NumberFromSS(std::string_view str, T fallback) noexcept
{
  std::istringstream iss{str.data()};
  T result{fallback};
  iss >> result;
  return result;
}
} // namespace

bool UTILS::STRING::ReplaceFirst(std::string& inputStr,
                                 std::string_view oldStr,
                                 std::string_view newStr)
{
  size_t start_pos = inputStr.find(oldStr);
  if (start_pos == std::string::npos)
    return false;
  inputStr.replace(start_pos, oldStr.length(), newStr);
  return true;
}

int UTILS::STRING::ReplaceAll(std::string& inputStr,
                              std::string_view oldStr,
                              std::string_view newStr)
{
  if (oldStr.empty())
    return 0;

  int replacedChars = 0;
  size_t index = 0;

  while (index < inputStr.size() && (index = inputStr.find(oldStr, index)) != std::string::npos)
  {
    inputStr.replace(index, oldStr.size(), newStr);
    index += newStr.size();
    replacedChars++;
  }
  return replacedChars;
}

std::string UTILS::STRING::ToDecimal(const uint8_t* data, size_t dataSize)
{
  std::stringstream ret;

  if (dataSize)
    ret << static_cast<unsigned int>(data[0]);

  for (size_t i{1}; i < dataSize; ++i)
  {
    ret << ',' << static_cast<unsigned int>(data[i]);
  }
  return ret.str();
}

unsigned char UTILS::STRING::ToHexNibble(char nibble)
{
  if (nibble >= '0' && nibble <= '9')
    return nibble - '0';
  else if (nibble >= 'a' && nibble <= 'f')
    return 10 + (nibble - 'a');
  else if (nibble >= 'A' && nibble <= 'F')
    return 10 + (nibble - 'A');
  return 0;
}

std::string UTILS::STRING::URLDecode(std::string_view strURLData)
// Taken from xbmc/URL.cpp
// modified to be more accommodating - if a non hex value follows a % take the characters directly and don't raise an error.
// However % characters should really be escaped like any other non safe character (www.rfc-editor.org/rfc/rfc1738.txt)
{
  std::string strResult;
  /* result will always be less than source */
  strResult.reserve(strURLData.length());

  for (unsigned int i = 0; i < strURLData.size(); ++i)
  {
    const char kar = strURLData[i];
    if (kar == '+')
    {
      strResult += ' ';
    }
    else if (kar == '%')
    {
      if (i < strURLData.size() - 2)
      {
        std::string strTmp{strURLData.substr(i + 1, 2)};
        int dec_num = -1;
        sscanf(strTmp.c_str(), "%x", reinterpret_cast<unsigned int*>(&dec_num));
        if (dec_num < 0 || dec_num > 255)
          strResult += kar;
        else
        {
          strResult += static_cast<char>(dec_num);
          i += 2;
        }
      }
      else
        strResult += kar;
    }
    else
      strResult += kar;
  }
  return strResult;
}

std::string UTILS::STRING::URLEncode(std::string_view strURLData)
{
  std::string result;

  for (auto c : strURLData)
  {
    // Don't URL encode "-_.!()" according to RFC1738
    // Don't URL encode "-_.~" according to RFC3986
    if (StringUtils::IsAsciiAlphaNum(c) || c == '-' || c == '.' || c == '_' || c == '!' ||
        c == '(' || c == ')' || c == '~')
    {
      result.push_back(c);
    }
    else
    {
      result.append("%");
      char buf[3];
      sprintf(buf, "%.2X", c);
      result.append(buf);
    }
  }
  return result;
}

uint64_t UTILS::STRING::ToUint64(std::string_view str, uint64_t fallback /* = 0 */)
{
  return NumberFromSS(str, fallback);
}
