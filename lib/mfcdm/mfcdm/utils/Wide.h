/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include <string>

#define NOGDI // Ignore useless header that creates useless macros
#include <windows.h>

namespace UTILS
{

  namespace WIDE
  {

  static std::wstring ConvertUtf8ToWide(std::string_view str)
  {
    const int charCount =
        MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.length()), nullptr, 0);
    if (charCount <= 0)
      return {};

    std::wstring wide(charCount, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.length()), wide.data(),
                        charCount);
    return wide;
  }

  static std::string ConvertWideToUTF8(std::wstring_view wstr)
  {
    const int charCount = WideCharToMultiByte(
        CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.length()), nullptr, 0, nullptr, nullptr);
    if (charCount <= 0)
      return {};

    std::string str(charCount, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.length()), str.data(),
                        charCount, nullptr, nullptr);
    return str;
  }

  } //namespace WIDE

} //namespace UTILS
