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
#include <cctype> // isspace
#include <charconv> // from_chars
#include <cstdio>
#include <cstring> // strstr
#include <iomanip>
#include <sstream>
#include <iterator>

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

uint32_t UTILS::STRING::ToUint32(std::string_view str, uint32_t fallback /* = 0 */)
{
  return NumberFromSS(str, fallback);
}

uint64_t UTILS::STRING::ToUint64(std::string_view str, uint64_t fallback /* = 0 */)
{
  return NumberFromSS(str, fallback);
}

double UTILS::STRING::ToDouble(std::string_view str, double fallback)
{
  return NumberFromSS(str, fallback);
}

float UTILS::STRING::ToFloat(std::string_view str, float fallback)
{
  return NumberFromSS(str, fallback);
}

int UTILS::STRING::ToInt32(std::string_view str, int fallback /* = 0 */)
{
  int result = fallback;
  std::from_chars(str.data(), str.data() + str.size(), result);
  return result;
}

bool UTILS::STRING::Contains(std::string_view str,
                             std::string_view keyword,
                             bool isCaseInsensitive /* = true */)
{
  if (isCaseInsensitive)
  {
    auto itStr = std::search(str.begin(), str.end(), keyword.begin(), keyword.end(),
                             [](unsigned char ch1, unsigned char ch2)
                             { return std::toupper(ch1) == std::toupper(ch2); });
    return (itStr != str.end());
  }

  return std::strstr(str.data(), keyword.data()) != nullptr;
}

bool UTILS::STRING::StartsWith(std::string_view str, std::string_view startStr)
{
  return str.substr(0, startStr.size()) == startStr;
}

std::set<std::string> UTILS::STRING::Split(std::string_view input,
                                             const char delimiter,
                                           int maxStrings /* = 0 */)
{
  std::set<std::string> result;
  StringUtils::SplitTo(std::inserter(result, result.end()), input.data(), delimiter, maxStrings);
  return result;
}

bool UTILS::STRING::Compare(std::string_view str1, std::string_view str2)
{
  return str1.compare(str2) == 0;
}

bool UTILS::STRING::CompareNoCase(std::string_view str1, std::string_view str2)
{
  if (str1.size() != str2.size())
    return false;
  return std::equal(str1.cbegin(), str1.cend(), str2.cbegin(),
                    [](std::string::value_type l, std::string::value_type r)
                    { return std::tolower(l) == std::tolower(r); });
}

bool UTILS::STRING::GetLine(std::stringstream& ss, std::string& line)
{
  do
  {
    if (!std::getline(ss, line))
      return false;

    // Trim return chars and spaces at the end of string
    size_t charPos = line.size();
    while (charPos &&
           (line[charPos - 1] == '\r' || line[charPos - 1] == '\n' || line[charPos - 1] == ' '))
    {
      charPos--;
    }
    line.resize(charPos);

    // Skip possible empty lines
  } while (line.empty());

  return true;
}

std::string UTILS::STRING::ToLower(std::string str)
{
  StringUtils::ToLower(str);
  return str;
}

std::map<std::string_view, std::string_view> UTILS::STRING::ToMap(std::string_view str,
                                                                  const char delimiter,
                                                                  const char separator)
{
  std::map<std::string_view, std::string_view> mapped;

  size_t keyPos = 0;
  size_t keyEnd;
  size_t valPos;
  size_t valEnd;

  while ((keyEnd = str.find(delimiter, keyPos)) != std::string::npos)
  {
    valPos = str.find_first_not_of(delimiter, keyEnd);
    if (valPos == std::string::npos)
      break;

    valEnd = str.find(separator, valPos);
    mapped.emplace(str.substr(keyPos, keyEnd - keyPos), str.substr(valPos, valEnd - valPos));

    keyPos = valEnd;
    if (keyPos != std::string::npos)
      ++keyPos;
  }

  return mapped;
}

std::string_view UTILS::STRING::Trim(std::string_view str)
{
  auto left = str.begin();
  while (left != str.end())
  {
    if (!std::isspace(*left))
      break;

    left++;
  }

  if (left == str.end())
    return {};

  auto right = str.end() - 1;
  while (right > left && std::isspace(*right))
  {
    right--;
  }

  //! @todo: when we will switch to C++20 replace return code with:
  //!   return {left, std::distance(left, right) + 1};
  return str.substr(left - str.begin(), std::distance(left, right) + 1);
}
