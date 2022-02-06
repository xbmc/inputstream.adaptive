/*
 *  Copyright (C) 2013 Jean-Luc Barriere
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#ifndef TSPACKET_H
#define TSPACKET_H

#include "tsTable.h"
#include "elementaryStream.h"

namespace TSDemux
{
  enum PACKET_TYPE
  {
    PACKET_TYPE_UNKNOWN = 0,
    PACKET_TYPE_PSI,
    PACKET_TYPE_PES
  };

  class Packet
  {
  public:
    Packet(void)
    : pid(0xffff)
    , continuity(0xff)
    , packet_type(PACKET_TYPE_UNKNOWN)
    , channel(0)
    , wait_unit_start(true)
    , has_stream_data(false)
    , streaming(false)
    , stream(NULL)
    , packet_table()
    {
    }

    ~Packet(void)
    {
      if (stream)
        delete stream;
    }

    void Reset(void)
    {
      continuity = 0xff;
      wait_unit_start = true;
      packet_table.Reset();
      if (stream)
        stream->Reset();
    }

    uint16_t pid;
    uint8_t continuity;
    PACKET_TYPE packet_type;
    uint16_t channel;
    bool wait_unit_start;
    bool has_stream_data;
    bool streaming;
    ElementaryStream* stream;
    TSTable packet_table;
  };
}

#endif /* TSPACKET_H */
