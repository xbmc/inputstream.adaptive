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

#include "ES_AC3.h"
#include "bitstream.h"

#include <algorithm>      // for max

using namespace TSDemux;

#define AC3_HEADER_SIZE 7

/** Channel mode (audio coding mode) */
typedef enum
{
  AC3_CHMODE_DUALMONO = 0,
  AC3_CHMODE_MONO,
  AC3_CHMODE_STEREO,
  AC3_CHMODE_3F,
  AC3_CHMODE_2F1R,
  AC3_CHMODE_3F1R,
  AC3_CHMODE_2F2R,
  AC3_CHMODE_3F2R
} AC3ChannelMode;

/* possible frequencies */
const uint16_t AC3SampleRateTable[3] = { 48000, 44100, 32000 };

/* possible bitrates */
const uint16_t AC3BitrateTable[19] = {
    32, 40, 48, 56, 64, 80, 96, 112, 128,
    160, 192, 224, 256, 320, 384, 448, 512, 576, 640
};

const uint8_t AC3ChannelsTable[8] = {
    2, 1, 2, 3, 3, 4, 4, 5
};

const uint16_t AC3FrameSizeTable[38][3] = {
    { 64,   69,   96   },
    { 64,   70,   96   },
    { 80,   87,   120  },
    { 80,   88,   120  },
    { 96,   104,  144  },
    { 96,   105,  144  },
    { 112,  121,  168  },
    { 112,  122,  168  },
    { 128,  139,  192  },
    { 128,  140,  192  },
    { 160,  174,  240  },
    { 160,  175,  240  },
    { 192,  208,  288  },
    { 192,  209,  288  },
    { 224,  243,  336  },
    { 224,  244,  336  },
    { 256,  278,  384  },
    { 256,  279,  384  },
    { 320,  348,  480  },
    { 320,  349,  480  },
    { 384,  417,  576  },
    { 384,  418,  576  },
    { 448,  487,  672  },
    { 448,  488,  672  },
    { 512,  557,  768  },
    { 512,  558,  768  },
    { 640,  696,  960  },
    { 640,  697,  960  },
    { 768,  835,  1152 },
    { 768,  836,  1152 },
    { 896,  975,  1344 },
    { 896,  976,  1344 },
    { 1024, 1114, 1536 },
    { 1024, 1115, 1536 },
    { 1152, 1253, 1728 },
    { 1152, 1254, 1728 },
    { 1280, 1393, 1920 },
    { 1280, 1394, 1920 },
};

const uint8_t EAC3Blocks[4] = {
  1, 2, 3, 6
};

typedef enum {
  EAC3_FRAME_TYPE_INDEPENDENT = 0,
  EAC3_FRAME_TYPE_DEPENDENT,
  EAC3_FRAME_TYPE_AC3_CONVERT,
  EAC3_FRAME_TYPE_RESERVED
} EAC3FrameType;

ES_AC3::ES_AC3(uint16_t pid)
 : ElementaryStream(pid)
{
  m_PTS                       = 0;
  m_DTS                       = 0;
  m_FrameSize                 = 0;
  m_SampleRate                = 0;
  m_Channels                  = 0;
  m_BitRate                   = 0;
  es_alloc_init               = 1920*2;
}

ES_AC3::~ES_AC3()
{
}

void ES_AC3::Parse(STREAM_PKT* pkt)
{
  int p = es_parsed;
  int l;
  while ((l = es_len - p) > 8)
  {
    if (FindHeaders(es_buf + p, l) < 0)
      break;
    p++;
  }
  es_parsed = p;

  if (es_found_frame && l >= m_FrameSize)
  {
    bool streamChange = SetAudioInformation(m_Channels, m_SampleRate, m_BitRate, 0, 0);
    pkt->pid            = pid;
    pkt->data           = &es_buf[p];
    pkt->size           = m_FrameSize;
    pkt->duration       = 90000 * 1536 / m_SampleRate;
    pkt->dts            = m_DTS;
    pkt->pts            = m_PTS;
    pkt->streamChange   = streamChange;

    es_consumed = p + m_FrameSize;
    es_parsed = es_consumed;
    es_found_frame = false;
  }
}

int ES_AC3::FindHeaders(uint8_t *buf, int buf_size)
{
  if (es_found_frame)
    return -1;

  if (buf_size < 9)
    return -1;

  uint8_t *buf_ptr = buf;

  if ((buf_ptr[0] == 0x0b && buf_ptr[1] == 0x77))
  {
    CBitstream bs(buf_ptr + 2, AC3_HEADER_SIZE * 8);

    // read ahead to bsid to distinguish between AC-3 and E-AC-3
    int bsid = bs.showBits(29) & 0x1F;
    if (bsid > 16)
      return 0;

    if (bsid <= 10)
    {
      // Normal AC-3
      bs.skipBits(16);
      int fscod       = bs.readBits(2);
      int frmsizecod  = bs.readBits(6);
      bs.skipBits(5); // skip bsid, already got it
      bs.skipBits(3); // skip bitstream mode
      int acmod       = bs.readBits(3);

      if (fscod == 3 || frmsizecod > 37)
        return 0;

      if (acmod == AC3_CHMODE_STEREO)
      {
        bs.skipBits(2); // skip dsurmod
      }
      else
      {
        if ((acmod & 1) && acmod != AC3_CHMODE_MONO)
          bs.skipBits(2);
        if (acmod & 4)
          bs.skipBits(2);
      }
      int lfeon = bs.readBits(1);

      int srShift   = std::max(bsid, 8) - 8;
      m_SampleRate  = AC3SampleRateTable[fscod] >> srShift;
      m_BitRate     = (AC3BitrateTable[frmsizecod>>1] * 1000) >> srShift;
      m_Channels    = AC3ChannelsTable[acmod] + lfeon;
      m_FrameSize   = AC3FrameSizeTable[frmsizecod][fscod] * 2;
    }
    else
    {
      // Enhanced AC-3
      int frametype = bs.readBits(2);
      if (frametype == EAC3_FRAME_TYPE_RESERVED)
        return 0;

       bs.readBits(3); // int substreamid

      m_FrameSize = (bs.readBits(11) + 1) << 1;
      if (m_FrameSize < AC3_HEADER_SIZE)
        return 0;

      int numBlocks = 6;
      int sr_code = bs.readBits(2);
      if (sr_code == 3)
      {
        int sr_code2 = bs.readBits(2);
        if (sr_code2 == 3)
          return 0;
        m_SampleRate = AC3SampleRateTable[sr_code2] / 2;
      }
      else
      {
        numBlocks = EAC3Blocks[bs.readBits(2)];
        m_SampleRate = AC3SampleRateTable[sr_code];
      }

      int channelMode = bs.readBits(3);
      int lfeon = bs.readBits(1);

      m_BitRate  = (uint32_t)(8.0 * m_FrameSize * m_SampleRate / (numBlocks * 256.0));
      m_Channels = AC3ChannelsTable[channelMode] + lfeon;
    }
    es_found_frame = true;
    m_DTS = c_pts;
    m_PTS = c_pts;
    c_pts += 90000 * 1536 / m_SampleRate;
    return -1;
  }
  return 0;
}

void ES_AC3::Reset()
{
  ElementaryStream::Reset();
}
