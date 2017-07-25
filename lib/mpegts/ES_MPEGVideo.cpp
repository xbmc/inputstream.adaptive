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

#include "ES_MPEGVideo.h"
#include "bitstream.h"
#include "debug.h"

using namespace TSDemux;

#define MPEG_PICTURE_START      0x00000100
#define MPEG_SEQUENCE_START     0x000001b3
#define MPEG_SEQUENCE_EXTENSION 0x000001b5
#define MPEG_SLICE_S            0x00000101
#define MPEG_SLICE_E            0x000001af

#define PKT_I_FRAME 1
#define PKT_P_FRAME 2
#define PKT_B_FRAME 3
#define PKT_NTYPES  4

/**
 * MPEG2VIDEO frame duration table (in 90kHz clock domain)
 */
const unsigned int mpeg2video_framedurations[16] = {
  0,
  3753,
  3750,
  3600,
  3003,
  3000,
  1800,
  1501,
  1500,
};

ES_MPEG2Video::ES_MPEG2Video(uint16_t pid)
 : ElementaryStream(pid)
{
  m_FrameDuration     = 0;
  m_vbvDelay          = -1;
  m_vbvSize           = 0;
  m_Height            = 0;
  m_Width             = 0;
  m_Dar               = 0.0f;
  m_DTS               = 0;
  m_PTS               = 0;
  m_AuDTS             = 0;
  m_AuPTS             = 0;
  m_AuPrevDTS         = 0;
  m_TemporalReference = 0;
  m_TrLastTime        = 0;
  m_PicNumber         = 0;
  m_FpsScale          = 0;
  es_alloc_init       = 80000;
  Reset();
}

ES_MPEG2Video::~ES_MPEG2Video()
{
}

void ES_MPEG2Video::Parse(STREAM_PKT *pkt)
{
  int frame_ptr = es_consumed;
  int p = es_parsed;
  uint32_t startcode = m_StartCode;
  bool frameComplete = false;
  int l;
  while ((l = es_len - p) > 3)
  {
    if ((startcode & 0xffffff00) == 0x00000100)
    {
      if (Parse_MPEG2Video(startcode, p, frameComplete) < 0)
      {
        break;
      }
    }
    startcode = startcode << 8 | es_buf[p++];
  }
  es_parsed = p;
  m_StartCode = startcode;

  if (frameComplete)
  {
    if (!m_NeedSPS && !m_NeedIFrame)
    {
      bool streamChange = false;
      if (es_frame_valid)
      {
        if (m_FpsScale == 0)
        {
          if (m_FrameDuration > 0)
            m_FpsScale = static_cast<int>(Rescale(m_FrameDuration, RESCALE_TIME_BASE, PTS_TIME_BASE));
          else
            m_FpsScale = 40000;
        }
        streamChange = SetVideoInformation(m_FpsScale, RESCALE_TIME_BASE, m_Height, m_Width, m_Dar, false);
      }

      pkt->pid          = pid;
      pkt->size         = es_consumed - frame_ptr;
      pkt->data         = &es_buf[frame_ptr];
      pkt->dts          = m_DTS;
      pkt->pts          = m_PTS;
      pkt->duration     = m_FrameDuration;
      pkt->streamChange = streamChange;
    }
    m_StartCode = 0xffffffff;
    es_parsed = es_consumed;
    es_found_frame = false;
    es_frame_valid = true;
  }
}

void ES_MPEG2Video::Reset()
{
  ElementaryStream::Reset();
  m_StartCode = 0xffffffff;
  m_NeedIFrame = true;
  m_NeedSPS = true;
}

int ES_MPEG2Video::Parse_MPEG2Video(uint32_t startcode, int buf_ptr, bool &complete)
{
  int len = es_len - buf_ptr;
  uint8_t *buf = es_buf + buf_ptr;

  switch (startcode & 0xFF)
  {
  case 0: // picture start
  {
    if (m_NeedSPS)
    {
      es_found_frame = true;
      return 0;
    }
    if (es_found_frame)
    {
      complete = true;
      es_consumed = buf_ptr - 4;
      return -1;
    }
    if (len < 4)
      return -1;
    if (!Parse_MPEG2Video_PicStart(buf))
      return 0;

    if (!es_found_frame)
    {
      m_AuPrevDTS = m_AuDTS;
      if (buf_ptr - 4 >= (int)es_pts_pointer)
      {
        m_AuDTS = c_dts != PTS_UNSET ? c_dts : c_pts;
        m_AuPTS = c_pts;
      }
      else
      {
        m_AuDTS = p_dts != PTS_UNSET ? p_dts : p_pts;
        m_AuPTS = p_pts;
      }
    }
    if (m_AuPrevDTS == m_AuDTS)
    {
      m_DTS = m_AuDTS + m_PicNumber*m_FrameDuration;
      m_PTS = m_AuPTS + (m_TemporalReference-m_TrLastTime)*m_FrameDuration;
    }
    else
    {
      m_PTS = m_AuPTS;
      m_DTS = m_AuDTS;
      m_PicNumber = 0;
      m_TrLastTime = m_TemporalReference;
    }

    m_PicNumber++;
    es_found_frame = true;
    break;
  }

  case 0xb3: // Sequence start code
  {
    if (es_found_frame)
    {
      complete = true;
      es_consumed = buf_ptr - 4;
      return -1;
    }
    if (len < 8)
      return -1;
    if (!Parse_MPEG2Video_SeqStart(buf))
      return 0;

    break;
  }

  case 0xb7: // sequence end
  {
    if (es_found_frame)
    {
      complete = true;
      es_consumed = buf_ptr;
      return -1;
    }
    break;
  }

  default:
    break;
  }

  return 0;
}

bool ES_MPEG2Video::Parse_MPEG2Video_SeqStart(uint8_t *buf)
{
  CBitstream bs(buf, 8 * 8);

  m_Width         = bs.readBits(12);
  m_Height        = bs.readBits(12);

  // figure out Display Aspect Ratio
  uint8_t aspect = bs.readBits(4);

  switch(aspect)
  {
    case 1:
      m_Dar = 1.0f;
      break;
    case 2:
      m_Dar = 4.0f/3.0f;
      break;
    case 3:
      m_Dar = 16.0f/9.0f;
      break;
    case 4:
      m_Dar = 2.21f;
      break;
    default:
      DBG(DEMUX_DBG_ERROR, "invalid / forbidden DAR in sequence header !\n");
      return false;
  }

  m_FrameDuration = mpeg2video_framedurations[bs.readBits(4)];
  bs.skipBits(18);
  bs.skipBits(1);

  m_vbvSize = bs.readBits(10) * 16 * 1024 / 8;
  m_NeedSPS = false;

  return true;
}

bool ES_MPEG2Video::Parse_MPEG2Video_PicStart(uint8_t *buf)
{
  CBitstream bs(buf, 4 * 8);

  m_TemporalReference = bs.readBits(10); /* temporal reference */

  int pct = bs.readBits(3);
  if (pct < PKT_I_FRAME || pct > PKT_B_FRAME)
    return true; /* Illegal picture_coding_type */

  if (pct == PKT_I_FRAME)
    m_NeedIFrame = false;

  int vbvDelay = bs.readBits(16); /* vbv_delay */
  if (vbvDelay  == 0xffff)
    m_vbvDelay = -1;
  else
    m_vbvDelay = vbvDelay;

  return true;
}
