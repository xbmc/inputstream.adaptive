/*
 *      Copyright (C) 2005-2013 Team XBMC
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

#ifndef ES_AAC_H
#define ES_AAC_H

#include "elementaryStream.h"
#include "bitstream.h"

namespace TSDemux
{
  class ES_AAC : public ElementaryStream
  {
  private:
    int         m_SampleRate;
    int         m_Channels;
    int         m_BitRate;
    int         m_FrameSize;

    int64_t     m_PTS;                /* pts of the current frame */
    int64_t     m_DTS;                /* dts of the current frame */

    bool        m_Configured;
    int         m_AudioMuxVersion_A;
    int         m_FrameLengthType;

    int FindHeaders(uint8_t *buf, int buf_size);
    bool ParseLATMAudioMuxElement(CBitstream *bs);
    void ReadStreamMuxConfig(CBitstream *bs);
    void ReadAudioSpecificConfig(CBitstream *bs);
    uint32_t LATMGetValue(CBitstream *bs) { return bs->readBits(bs->readBits(2) * 8); }

  public:
    ES_AAC(uint16_t pes_pid);
    virtual ~ES_AAC();

    virtual void Parse(STREAM_PKT* pkt);
    virtual void Reset();
  };
}

#endif /* ES_AAC_H */
