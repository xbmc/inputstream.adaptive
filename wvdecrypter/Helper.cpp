/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Helper.h"

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
