/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Segment.h"
#include "AdaptiveUtils.h"

#include "kodi/tools/StringUtils.h"

using namespace PLAYLIST;
using namespace kodi::tools;

void PLAYLIST::CSegment::Copy(const CSegment* src)
{
  *this = *src;
}
