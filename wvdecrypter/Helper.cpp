/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Helper.h"

#include <cstdio> // fopen

SSD::SSD_HOST* GLOBAL::Host = nullptr;

void LOG::Log(SSD::SSDLogLevel level, const char* format, ...)
{
  if (!GLOBAL::Host)
    return;

  va_list args;
  va_start(args, format);
  GLOBAL::Host->LogVA(level, format, args);
  va_end(args);
}

//! @todo: with ssd_wv refactor we must use the file management of the IA interface
//!        so this method will be removed in favour of UTILS::FILESYS::SaveFile
void SSD_UTILS::SaveFile(std::string_view filePath, std::string_view data)
{
  FILE* f = std::fopen(filePath.data(), "wb");
  if (f)
  {
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
  }
  else
    LOG::LogF(SSD::SSDLogLevel::SSDERROR, "Cannot open file \"%s\" for writing.", filePath.data());
}
