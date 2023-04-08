/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AdaptiveUtils.h"

#include <cstdio> // sscanf
#include <inttypes.h>

bool PLAYLIST::ParseRangeRFC(std::string_view range, uint64_t& start, uint64_t& end)
{
  //! @todo: must be reworked as https://httpwg.org/specs/rfc7233.html
  uint64_t startVal;
  uint64_t endVal;
  if (std::sscanf(range.data(), "%" SCNu64 "-%" SCNu64, &startVal, &endVal) > 0)
  {
    start = startVal;
    end = endVal;
    return true;
  }
  return false;
}

bool PLAYLIST::ParseRangeValues(std::string_view range,
                                uint64_t& first,
                                uint64_t& second,
                                char separator /* = '@' */)
{
  std::string pattern = "%llu";
  pattern.push_back(separator);
  pattern.append("%llu");

  if (std::sscanf(range.data(), pattern.c_str(), &first, &second) > 0)
    return true;

  return false;
}
