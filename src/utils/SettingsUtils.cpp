/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SettingsUtils.h"

using namespace UTILS::SETTINGS;

bool UTILS::SETTINGS::ParseResolutionLimit(std::string_view resStr, std::pair<int, int>& res)
{
  auto mapIt{RESOLUTION_LIMITS.find(resStr)};

  if (mapIt != RESOLUTION_LIMITS.end())
  {
    res = mapIt->second;
    return true;
  }

  return false;
}
