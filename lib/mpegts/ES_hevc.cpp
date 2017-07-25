/*
 *      Copyright (C) 2005-2012 Team XBMC
 *      Copyright (C) 2015 Team KODI
 *
 *      http://kodi.tv
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
 *  along with KODI; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "ES_hevc.h"
#include "bitstream.h"
#include "debug.h"

#include <cstring>      // for memset memcpy

using namespace TSDemux;

ES_hevc::ES_hevc(uint16_t pes_pid)
 : ElementaryStream(pes_pid)
{
  m_Height            = 0;
  m_Width             = 0;
  m_FpsScale          = 0;
  m_PixelAspect.den   = 1;
  m_PixelAspect.num   = 0;
  m_DTS               = PTS_UNSET;
  m_PTS               = PTS_UNSET;
  m_Interlaced        = false;
  es_alloc_init       = 240000;
  Reset();
}

ES_hevc::~ES_hevc()
{
}

void ES_hevc::Parse(STREAM_PKT* pkt)
{
  if (es_parsed + 10 > es_len) // 2*startcode + header + trail bits
    return;

  size_t frame_ptr = es_consumed;
  size_t p = es_parsed;
  uint32_t startcode = m_StartCode;
  bool frameComplete = false;

  while (p < es_len)
  {
    startcode = startcode << 8 | es_buf[p++];
    if ((startcode & 0x00ffffff) == 0x00000001)
    {
      if (m_LastStartPos != -1)
         Parse_HEVC(frame_ptr + m_LastStartPos, p - frame_ptr - m_LastStartPos, frameComplete);
      m_LastStartPos = p - frame_ptr; // save relative position from start
      if (frameComplete)
        break;
    }
  }
  es_parsed = p;
  m_StartCode = startcode;

  if (frameComplete)
  {
    if (!m_NeedSPS)
    {
      double PAR = (double)m_PixelAspect.num/(double)m_PixelAspect.den;
      double DAR = (PAR * m_Width) / m_Height;
      DBG(DEMUX_DBG_DEBUG, "HEVC SPS: PAR %i:%i\n", m_PixelAspect.num, m_PixelAspect.den);
      DBG(DEMUX_DBG_DEBUG, "HEVC SPS: DAR %.2f\n", DAR);

      uint64_t duration;
      if (c_dts != PTS_UNSET && p_dts != PTS_UNSET && c_dts > p_dts)
        duration = c_dts - p_dts;
      else
        duration = static_cast<int>(Rescale(20000, PTS_TIME_BASE, RESCALE_TIME_BASE));

      bool streamChange = false;
      if (es_frame_valid)
      {
        if (m_FpsScale == 0)
          m_FpsScale = static_cast<int>(Rescale(duration, RESCALE_TIME_BASE, PTS_TIME_BASE));
        streamChange = SetVideoInformation(m_FpsScale, RESCALE_TIME_BASE, m_Height, m_Width, static_cast<float>(DAR), m_Interlaced);
      }

      pkt->pid      = pid;
      pkt->size     = es_consumed - frame_ptr;
      pkt->data     = &es_buf[frame_ptr];
      pkt->dts      = m_DTS;
      pkt->pts      = m_PTS;
      pkt->duration = duration;
      pkt->streamChange = streamChange;
    }
    m_StartCode = 0xffffffff;
    m_LastStartPos = -1;
    es_parsed = es_consumed;
    es_found_frame = false;
    es_frame_valid = true;
  }
}

void ES_hevc::Reset()
{
  ElementaryStream::Reset();
  m_StartCode = 0xffffffff;
  m_LastStartPos = -1;
  m_NeedSPS = true;
  m_NeedPPS = true;
  memset(&m_streamData, 0, sizeof(m_streamData));
}


void ES_hevc::Parse_HEVC(int buf_ptr, unsigned int NumBytesInNalUnit, bool &complete)
{
  uint8_t *buf = es_buf + buf_ptr;
  uint16_t header;
  HDR_NAL hdr;

  // nal_unit_header
  header = (buf[0] << 8) | buf[1];
  if (header & 0x8000) // ignore forbidden_bit == 1
    return;
  hdr.nal_unit_type   = (header & 0x7e00) >> 9;
  hdr.nuh_layer_id    = (header &  0x1f8) >> 3;
  hdr.nuh_temporal_id = (header &    0x7) - 1;

  if (hdr.nal_unit_type <= NAL_CRA_NUT)
  {
    if (m_NeedSPS || m_NeedPPS)
    {
      es_found_frame = true;
      return;
    }
    hevc_private::VCL_NAL vcl;
    memset(&vcl, 0, sizeof(hevc_private::VCL_NAL));
    Parse_SLH(buf, NumBytesInNalUnit, hdr, vcl);

    // check for the beginning of a new access unit
    if (es_found_frame && IsFirstVclNal(vcl))
    {
      complete = true;
      es_consumed = buf_ptr - 3;
      return;
    }

    if (!es_found_frame)
    {
      if (buf_ptr - 3 >= (int)es_pts_pointer)
      {
        m_DTS = c_dts;
        m_PTS = c_pts;
      }
      else
      {
        m_DTS = p_dts;
        m_PTS = p_pts;
      }
    }

    m_streamData.vcl_nal = vcl;
    es_found_frame = true;
  }
  else
  {
    switch (hdr.nal_unit_type)
    {
    case NAL_VPS_NUT:
       break;

    case NAL_SPS_NUT:
    {
      if (es_found_frame)
      {
        complete = true;
        es_consumed = buf_ptr - 3;
        return;
      }
      Parse_SPS(buf, NumBytesInNalUnit, hdr);
      m_NeedSPS = false;
      break;
    }

    case NAL_PPS_NUT:
    {
      if (es_found_frame)
      {
        complete = true;
        es_consumed = buf_ptr - 3;
        return;
      }
      Parse_PPS(buf, NumBytesInNalUnit);
      m_NeedPPS = false;
      break;
    }

    case NAL_AUD_NUT:
      if (es_found_frame && (p_pts != PTS_UNSET))
      {
        complete = true;
        es_consumed = buf_ptr - 3;
      }
      break;

    case NAL_EOS_NUT:
      if (es_found_frame)
      {
        complete = true;
        es_consumed = buf_ptr + 2;
      }
      break;

    case NAL_FD_NUT:
      break;

    case NAL_PFX_SEI_NUT:
      if (es_found_frame)
      {
        complete = true;
        es_consumed = buf_ptr - 3;
      }
      break;

    case NAL_SFX_SEI_NUT:
       break;

    default:
      DBG(DEMUX_DBG_INFO, "HEVC fixme: nal unknown %i\n", hdr.nal_unit_type);
      break;
    }
  }
}

void ES_hevc::Parse_PPS(uint8_t *buf, int len)
{
  CBitstream bs(buf, len*8, true);

  int pps_id = bs.readGolombUE();
  int sps_id = bs.readGolombUE();
  m_streamData.pps[pps_id].sps = sps_id;
  m_streamData.pps[pps_id].dependent_slice_segments_enabled_flag = bs.readBits(1);
}

void ES_hevc::Parse_SLH(uint8_t *buf, int len, HDR_NAL hdr, hevc_private::VCL_NAL &vcl)
{
  CBitstream bs(buf, len*8, true);

  vcl.nal_unit_type = hdr.nal_unit_type;

  vcl.first_slice_segment_in_pic_flag = bs.readBits(1);

  if ((hdr.nal_unit_type >= NAL_BLA_W_LP) && (hdr.nal_unit_type <= NAL_RSV_IRAP_VCL23))
    bs.skipBits(1); // no_output_of_prior_pics_flag

  vcl.pic_parameter_set_id = bs.readGolombUE();
}

// 7.3.2.2.1 General sequence parameter set RBSP syntax
void ES_hevc::Parse_SPS(uint8_t *buf, int len, HDR_NAL hdr)
{
  CBitstream bs(buf, len*8, true);
  unsigned int i;
  int sub_layer_profile_present_flag[8], sub_layer_level_present_flag[8];

  bs.skipBits(4); // sps_video_parameter_set_id

  unsigned int sps_max_sub_layers_minus1 = bs.readBits(3);
  bs.skipBits(1); // sps_temporal_id_nesting_flag

  // skip over profile_tier_level
  bs.skipBits(8 + 32 + 4 + 43 + 1 +8);
  for (i=0; i<sps_max_sub_layers_minus1; i++)
  {
    sub_layer_profile_present_flag[i] = bs.readBits(1);
    sub_layer_level_present_flag[i] = bs.readBits(1);
  }
  if (sps_max_sub_layers_minus1 > 0)
  {
    for (i=sps_max_sub_layers_minus1; i<8; i++)
      bs.skipBits(2);
  }
  for (i=0; i<sps_max_sub_layers_minus1; i++)
  {
    if (sub_layer_profile_present_flag[i])
      bs.skipBits(8 + 32 + 4 + 43 + 1);
    if (sub_layer_level_present_flag[i])
      bs.skipBits(8);
  }
  // end skip over profile_tier_level

  bs.readGolombUE(); // sps_seq_parameter_set_id
  unsigned int chroma_format_idc = bs.readGolombUE();

  if (chroma_format_idc == 3)
    bs.skipBits(1); // separate_colour_plane_flag

  m_Width  = bs.readGolombUE();
  m_Height = bs.readGolombUE();
  m_PixelAspect.num = 1;
}

bool ES_hevc::IsFirstVclNal(hevc_private::VCL_NAL &vcl)
{
  if (m_streamData.vcl_nal.pic_parameter_set_id != vcl.pic_parameter_set_id)
    return true;

  if (vcl.first_slice_segment_in_pic_flag)
    return true;

  return false;
}

