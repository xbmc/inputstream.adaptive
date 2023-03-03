/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../src/SSD_dll.h"

namespace GLOBAL
{
// Give shared access to the host interface
extern SSD::SSD_HOST* Host;
} // namespace GLOBAL

namespace LOG
{
void Log(SSD::SSDLogLevel level, const char* format, ...);

#define LogF(level, format, ...) Log((level), ("%s: " format), __FUNCTION__, ##__VA_ARGS__)

} // namespace LOG

namespace SSD_UTILS
{
void SaveFile(std::string_view filePath, std::string_view data);
}
