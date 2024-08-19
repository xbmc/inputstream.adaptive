/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace UTILS
{
namespace STRING
{

// \brief Template function to check if a key exists in a container e.g. <map>
template<typename T, typename Key>
bool KeyExists(const T& container, const Key& key)
{
  return container.find(key) != std::end(container);
}

template<typename T>
bool KeyExists(const T& container, const std::string_view key)
{
  return container.find(key.data()) != std::end(container);
}

/*!
 * \brief Get map value of the specified key
 * \param map The map where find the value
 * \param key The key to find
 * \param val[OUT] The value that match to the specified key, if found
 * \return True if found, otherwise false.
 */
template<typename T, typename TValue>
bool GetMapValue(const std::map<T, TValue>& map, const T& key, TValue& val)
{
  auto mapIt = map.find(key);
  if (mapIt != map.cend())
  {
    val = mapIt->second;
    return true;
  }
  return false;
}

/*!
 * \brief Replace the first string occurrence in a string
 * \param inputStr String to perform the replace
 * \param oldStr String to find
 * \param newStr String used to replace the old one
 * \return true if the string has been replaced, false otherwise
 */
bool ReplaceFirst(std::string& inputStr, std::string_view oldStr, std::string_view newStr);

/*!
 * \brief Replace all string occurrences in a string
 * \param inputStr The string to perform the replace
 * \param oldStr String to find
 * \param newStr String used to replace the old one
 * \return The number of chars replaced
 */
int ReplaceAll(std::string& inputStr, std::string_view oldStr, std::string_view newStr);

/*!
 * \brief Convert chars to a decimal string separated by a comma
 * \param data The chars data to convert
 * \param dataSize The size of the data
 * \return The converted string (e.g. "75,111,100,105")
 */
std::string ToDecimal(const uint8_t* data, size_t dataSize);

/*!
 * \brief Convert an hex (ASCII) nibble to an unsigned char
 * \param nibble The hex nibble char
 * \return The hex nibble converted
 */
unsigned char ToHexNibble(char nibble);

/*!
 * \brief Decode an URL-encoded string
 * \param strURLData String to be decoded
 * \return The string decoded
 */
std::string URLDecode(std::string_view strURLData);

/*!
 * \brief URL-encode a string
 * \param strURLData String to be encoded
 * \return The string URL-encoded
 */
std::string URLEncode(std::string_view strURLData);

/*!
 * \brief Converts a string to unsigned int32 without throw exceptions.
 * \param str The number as string
 * \param fallback [OPT] The value returned if the parsing fails.
 * \return The resulting number, if fails return the fallback value.
 */
uint32_t ToUint32(std::string_view str, uint32_t fallback = 0);

/*!
 * \brief Converts a string to unsigned int64 without throw exceptions.
 * \param str The number as string
 * \param fallback [OPT] The value returned if the parsing fails.
 * \return The resulting number, if fails return the fallback value.
 */
uint64_t ToUint64(std::string_view str, uint64_t fallback = 0);

/*!
 * \brief Converts a string to double without throw exceptions.
 * \param str The number as string
 * \param fallback [OPT] The value returned if the parsing fails.
 * \return The resulting number, if fails return the fallback value.
 */
double ToDouble(std::string_view str, double fallback = 0);

/*!
 * \brief Converts a string to float without throw exceptions.
 * \param str The number as string
 * \param fallback [OPT] The value returned if the parsing fails.
 * \return The resulting number, if fails return the fallback value.
 */
float ToFloat(std::string_view str, float fallback = 0);

int ToInt32(std::string_view str, int fallback = 0);

/*!
 * \brief Check if a keyword string is contained on another string.
 * \param str The string in which to search for the keyword
 * \param keyword The string to search for
 * \return True if the keyword if found.
 */
bool Contains(std::string_view str, std::string_view keyword, bool isCaseInsensitive = true);

/*!
 * \brief Checks a string for the begin of another string.
 * \param str String to be checked
 * \param startStr String with which text in str is checked at the beginning
 * \return True if string started with asked text, false otherwise
 */
bool StartsWith(std::string_view str, std::string_view startStr);

/*!
 * \brief Splits the given input string using the given delimiter
 *        into separate, unique, strings.
 * \param input Input string to be split
 * \param delimiter Delimiter to be used to split the input string
 * \param maxStrings [OPT] Maximum number of resulting split strings
 * \return List of splitted unique strings.
 */
std::set<std::string> SplitToSet(std::string_view input, const char delimiter, int maxStrings = 0);

/*!
 * \brief Splits the given input string using the given delimiter into separate strings.
 * \param input Input string to be split
 * \param delimiter Delimiter to be used to split the input string
 * \param maxStrings [OPT] Maximum number of resulting split strings
 * \return List of splitted strings.
 */
std::vector<std::string> SplitToVec(std::string_view input, const char delimiter, int maxStrings = 0);

/*!
 * \brief Compares two strings in case sensitive way
 * \param str1 String to be compared
 * \param str2 String to be compared
 * \return True if strings are equal.
 */
bool Compare(std::string_view str1, std::string_view str2);

/*!
 * \brief Compares two strings in case insensitive way
 * \param str1 String to be compared
 * \param str2 String to be compared
 * \return True if strings are equal.
 */
bool CompareNoCase(std::string_view str1, std::string_view str2);

/*!
 * \brief Get a line from string stream, by skipping empty lines.
 * \param ss Input stringstream
 * \param line[OUT] A line string read
 * \return True when the next line is read, otherwise false when EOF.
 */
bool GetLine(std::stringstream& ss, std::string& line);

/*!
 * \brief Convert a string to lower.
 * \param str The string to be converted
 * \return The string in lowercase.
 */
std::string ToLower(std::string str);

/*!
 * \brief Convert a hex value as string to unsigned integer.
 * \param hexValue The hex value as string to be converted
 * \return The hex value if has success, otherwise 0.
 */
uint32_t HexStrToUint(std::string_view hexValue);

/*!
 * \brief Convert a string value to hex bytes.
 * \param str The string to be converted
 * \param bytes[OUT] The converted string to hex bytes
 * \return True if has success, otherwise false.
 */
bool ToHexBytes(const std::string& str, std::vector<uint8_t>& bytes);

/*!
 * \brief Convert a string in to a vector of uint8_t
 * \param str The string to be converted
 * \return The converted data
 */
std::vector<uint8_t> ToVecUint8(std::string_view str);

/*!
 * \brief Convert each character in the string to its hexadecimal
 *        representation and return the concatenated result.
 *        Example: "abc" -> "616263"
 * \param str The string to be converted
 * \return The string on its hexadecimal representation
 */
std::string ToHexadecimal(std::string_view str);

/*!
 * \brief Convert each character in the array to its hexadecimal
 *        representation and return the concatenated result.
 *        Example: "abc" -> "616263"
 * \param str The array to be converted
 * \param size The size of the array
 * \return The string on its hexadecimal representation
 */
std::string ToHexadecimal(const uint8_t* str, const size_t size);

/*!
 * \brief Convert each character in the vector to its hexadecimal
 *        representation and return the concatenated result.
 *        Example: "abc" -> "616263"
 * \param str The array to be converted
 * \param size The size of the array
 * \return The string on its hexadecimal representation
 */
std::string ToHexadecimal(const std::vector<uint8_t> data);

/*!
 * \brief Trim a string with remove of not wanted spaces at begin and end of string.
 * \param value The string to be trimmed
 * \return The string trimmed
 */
std::string Trim(std::string value);

} // namespace STRING
} // namespace UTILS
