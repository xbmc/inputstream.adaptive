/*
 *      Copyright (C) 2013 Jean-Luc Barriere
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301 USA
 *  http://www.gnu.org/copyleft/gpl.html
 *
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
