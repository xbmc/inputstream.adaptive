/*
 *      Copyright (C) 2005-2012 Team XBMC
 *      http://www.xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#ifndef BITSTREAM_H
#define BITSTREAM_H

#include <inttypes.h>
#include <stddef.h>

namespace TSDemux
{
  class CBitstream
  {
  private:
    uint8_t       *m_data;
    size_t         m_offset;
    const size_t   m_len;
    bool           m_error;
    const bool     m_doEP3;

  public:
    CBitstream(uint8_t *data, size_t bits)
    : m_data(data)
    , m_offset(0)
    , m_len(bits)
    , m_error(false)
    , m_doEP3(false)
    {}

    // this is a bitstream that has embedded emulation_prevention_three_byte
    // sequences that need to be removed as used in HECV.
    // Data must start at byte 2
    CBitstream(uint8_t *data, size_t bits, bool doEP3)
    : m_data(data)
    , m_offset(16) // skip header and use as sentinel for EP3 detection
    , m_len(bits)
    , m_error(false)
    , m_doEP3(true)
    {}

    void         skipBits(unsigned int num);
    unsigned int readBits(int num);
    unsigned int showBits(int num);
    unsigned int readBits1() { return readBits(1); }
    unsigned int readGolombUE(int maxbits = 32);
    signed int   readGolombSE();
    size_t       length() { return m_len; }
    bool         isError() { return m_error; }
  };
}

#endif /* BITSTREAM_H */
