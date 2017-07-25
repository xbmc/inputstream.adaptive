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

#include "ES_MPEGAudio.h"
#include "bitstream.h"

using namespace TSDemux;

const uint16_t FrequencyTable[3] = { 44100, 48000, 32000 };
const uint16_t BitrateTable[2][3][15] =
{
  {
    {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
    {0, 32, 48, 56, 64,  80,  96,  112, 128, 160, 192, 224, 256, 320, 384 },
    {0, 32, 40, 48, 56,  64,  80,  96,  112, 128, 160, 192, 224, 256, 320 }
  },
  {
    {0, 32, 48, 56, 64,  80,  96,  112, 128, 144, 160, 176, 192, 224, 256},
    {0, 8,  16, 24, 32,  40,  48,  56,  64,  80,  96,  112, 128, 144, 160},
    {0, 8,  16, 24, 32,  40,  48,  56,  64,  80,  96,  112, 128, 144, 160}
  }
};

ES_MPEG2Audio::ES_MPEG2Audio(uint16_t pid)
 : ElementaryStream(pid)
{
  m_PTS                       = 0;
  m_DTS                       = 0;
  m_FrameSize                 = 0;
  m_SampleRate                = 0;
  m_Channels                  = 0;
  m_BitRate                   = 0;
  es_alloc_init               = 2048;
}

ES_MPEG2Audio::~ES_MPEG2Audio()
{
}

void ES_MPEG2Audio::Parse(STREAM_PKT* pkt)
{
  int p = es_parsed;
  int l;
  while ((l = es_len - p) > 3)
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
    pkt->duration       = 90000 * 1152 / m_SampleRate;
    pkt->dts            = m_DTS;
    pkt->pts            = m_PTS;
    pkt->streamChange   = streamChange;

    es_consumed = p + m_FrameSize;
    es_parsed = es_consumed;
    es_found_frame = false;
  }
}

int ES_MPEG2Audio::FindHeaders(uint8_t *buf, int buf_size)
{
  if (es_found_frame)
    return -1;

  if (buf_size < 4)
    return -1;

  uint8_t *buf_ptr = buf;

  if ((buf_ptr[0] == 0xFF && (buf_ptr[1] & 0xE0) == 0xE0))
  {
    CBitstream bs(buf_ptr, 4 * 8);
    bs.skipBits(11); // syncword

    int audioVersion = bs.readBits(2);
    if (audioVersion == 1)
      return 0;
    int mpeg2 = !(audioVersion & 1);
    int mpeg25 = !(audioVersion & 3);

    int layer = bs.readBits(2);
    if (layer == 0)
      return 0;
    layer = 4 - layer;

    bs.skipBits(1); // protetion bit
    int bitrate_index = bs.readBits(4);
    if (bitrate_index == 15 || bitrate_index == 0)
      return 0;
    m_BitRate  = BitrateTable[mpeg2][layer - 1][bitrate_index] * 1000;

    int sample_rate_index = bs.readBits(2);
    if (sample_rate_index == 3)
      return 0;
    m_SampleRate = FrequencyTable[sample_rate_index] >> (mpeg2 + mpeg25);

    int padding = bs.readBits1();
    bs.skipBits(1); // private bit
    int channel_mode = bs.readBits(2);

    if (channel_mode == 11)
      m_Channels = 1;
    else
      m_Channels = 2;

    if (layer == 1)
      m_FrameSize = (12 * m_BitRate / m_SampleRate + padding) * 4;
    else
      m_FrameSize = 144 * m_BitRate / m_SampleRate + padding;

    es_found_frame = true;
    m_DTS = c_pts;
    m_PTS = c_pts;
    c_pts += 90000 * 1152 / m_SampleRate;
    return -1;
  }
  return 0;
}
