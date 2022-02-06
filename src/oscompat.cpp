/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#ifndef _WIN32
#include <stdlib.h>
#include "oscompat.h"

time_t _mkgmtime(struct tm *tm)
{
  return timegm(tm);
}
#endif
