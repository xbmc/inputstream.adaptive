/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#ifndef INPUTSTREAM_TEST_BUILD
#include <kodi/AddonBase.h>
#else
#include "kodi/tools/StringUtils.h"
#include <iostream>
#endif

#include <utility>

// To keep in sync with other interfaces:
// SSDLogLevel on SSD_dll.h
// CDMLogLevel on cdm_adapter.h
enum LogLevel
{
  LOGDEBUG,
  LOGINFO,
  LOGWARNING,
  LOGERROR,
  LOGFATAL
};

namespace LOG
{

template<typename... Args>
inline void Log(const LogLevel level, const char* format, Args&&... args)
{
#ifndef INPUTSTREAM_TEST_BUILD
  ADDON_LOG addonLevel;

  switch (level)
  {
    case LogLevel::LOGFATAL:
      addonLevel = ADDON_LOG::ADDON_LOG_FATAL;
      break;
    case LogLevel::LOGERROR:
      addonLevel = ADDON_LOG::ADDON_LOG_ERROR;
      break;
    case LogLevel::LOGWARNING:
      addonLevel = ADDON_LOG::ADDON_LOG_WARNING;
      break;
    case LogLevel::LOGINFO:
      addonLevel = ADDON_LOG::ADDON_LOG_INFO;
      break;
    default:
      addonLevel = ADDON_LOG::ADDON_LOG_DEBUG;
  }

  kodi::Log(addonLevel, format, std::forward<Args>(args)...);
#else
  std::string logStr = kodi::tools::StringUtils::Format(format, std::forward<Args>(args)...);
  switch (level)
  {
  case LogLevel::LOGFATAL:
    std::cout << "[ LOG-FATAL ] " << logStr << std::endl;
    break;
  case LogLevel::LOGERROR:
    std::cout << "[ LOG-ERROR ] " << logStr << std::endl;
    break;
/*
  case LogLevel::LOGWARNING:
    std::cout << "[ LOG-WARN  ] " << logStr << std::endl;
    break;
  case LogLevel::LOGINFO:
    std::cout << "[ LOG-INFO  ] " << logStr << std::endl;
    break;
  default:
    std::cout << "[ LOG-DEBUG ] " << logStr << std::endl;
*/
  }
#endif
}

#define LogF(level, format, ...) Log((level), ("%s: " format), __FUNCTION__, ##__VA_ARGS__)

} // namespace LOG
