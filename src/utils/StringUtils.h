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
namespace STRING
{

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
 * \brief Converts a string to unsigned int64 without throw exceptions.
 * \param str The number as string
 * \param fallback [OPT] The value returned if the parsing fails.
 * \return The resulting number, if fails return the fallback value.
 */
uint64_t ToUint64(std::string_view str, uint64_t fallback = 0);

/*!
 * \brief Compares two strings in case insensitive way
 * \param str1 String to be compared
 * \param str2 String to be compared
 * \return True if strings are equal.
 */
bool CompareNoCase(std::string_view str1, std::string_view str2);

} // namespace STRING
} // namespace UTILS
