/*
 *  Copyright (C) 2013 Jean-Luc Barriere
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#ifndef TSTABLE_H
#define TSTABLE_H

#include <inttypes.h>
#include <cstring>        // for memset

// PSI section size (EN 300 468)
#define TABLE_BUFFER_SIZE       4096

namespace TSDemux
{
  class TSTable
  {
  public:
    uint8_t table_id;
    uint8_t version;
    uint16_t id;
    uint16_t len;
    uint16_t offset;
    unsigned char buf[TABLE_BUFFER_SIZE];

    TSTable(void)
    : table_id(0xff)
    , version(0xff)
    , id(0xffff)
    , len(0)
    , offset(0)
    {
      memset(buf, 0, TABLE_BUFFER_SIZE);
    }

    void Reset(void)
    {
      len = 0;
      offset = 0;
    }
  };
}

#endif /* TSTABLE_H */

