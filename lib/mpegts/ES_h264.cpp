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

#include "ES_h264.h"
#include "bitstream.h"
#include "debug.h"

#include <cstring>      // for memset memcpy

using namespace TSDemux;

static const int h264_lev2cpbsize[][2] =
{
  {10, 175},
  {11, 500},
  {12, 1000},
  {13, 2000},
  {20, 2000},
  {21, 4000},
  {22, 4000},
  {30, 10000},
  {31, 14000},
  {32, 20000},
  {40, 25000},
  {41, 62500},
  {42, 62500},
  {50, 135000},
  {51, 240000},
  {-1, -1},
};

ES_h264::ES_h264(uint16_t pes_pid)
 : ElementaryStream(pes_pid)
{
  m_Height                      = 0;
  m_Width                       = 0;
  m_FPS                         = 25;
  m_FpsScale                    = 0;
  m_FrameDuration               = 0;
  m_vbvDelay                    = -1;
  m_vbvSize                     = 0;
  m_PixelAspect.den             = 1;
  m_PixelAspect.num             = 0;
  m_DTS                         = 0;
  m_PTS                         = 0;
  m_Interlaced                  = false;
  es_alloc_init                 = 240000;
  Reset();
}

ES_h264::~ES_h264()
{
}

void ES_h264::Parse(STREAM_PKT* pkt)
{
  size_t frame_ptr = es_consumed;
  size_t p = es_parsed;
  uint32_t startcode = m_StartCode;
  bool frameComplete = false;

  while ((p + 3) < es_len)
  {
    if ((startcode & 0xffffff00) == 0x00000100)
    {
      if (Parse_H264(startcode, p, frameComplete) < 0)
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
      double PAR = (double)m_PixelAspect.num/(double)m_PixelAspect.den;
      double DAR = (PAR * m_Width) / m_Height;
      DBG(DEMUX_DBG_PARSE, "H.264 SPS: PAR %i:%i\n", m_PixelAspect.num, m_PixelAspect.den);
      DBG(DEMUX_DBG_PARSE, "H.264 SPS: DAR %.2f\n", DAR);

      uint64_t duration;
      if (c_dts != PTS_UNSET && p_dts != PTS_UNSET && c_dts > p_dts)
        duration = c_dts - p_dts;
      else
        duration = static_cast<int>(Rescale(40000, PTS_TIME_BASE, RESCALE_TIME_BASE));

      bool streamChange = false;
      if (es_frame_valid)
      {
        if (m_FpsScale == 0)
          m_FpsScale = static_cast<int>(Rescale(duration, RESCALE_TIME_BASE, PTS_TIME_BASE));
        streamChange = SetVideoInformation(m_FpsScale, RESCALE_TIME_BASE, m_Height, m_Width, static_cast<float>(DAR), m_Interlaced);
      }

      pkt->pid            = pid;
      pkt->size           = es_consumed - frame_ptr;
      pkt->data           = &es_buf[frame_ptr];
      pkt->dts            = m_DTS;
      pkt->pts            = m_PTS;
      pkt->duration       = duration;
      pkt->streamChange   = streamChange;
    }
    m_StartCode = 0xffffffff;
    es_parsed = es_consumed;
    es_found_frame = false;
    es_frame_valid = true;
  }
}

void ES_h264::Reset()
{
  ElementaryStream::Reset();
  m_StartCode = 0xffffffff;
  m_NeedIFrame = true;
  m_NeedSPS = true;
  m_NeedPPS = true;
  memset(&m_streamData, 0, sizeof(m_streamData));
}

int ES_h264::Parse_H264(uint32_t startcode, int buf_ptr, bool &complete)
{
  int len = es_len - buf_ptr;
  uint8_t *buf = es_buf + buf_ptr;

  switch(startcode & 0x9f)
  {
  case 1:
  case 2:
  case 3:
  case 4:
  case 5:
  {
    if (m_NeedSPS || m_NeedPPS)
    {
      es_found_frame = true;
      return 0;
    }
    // need at least 32 bytes for parsing nal
    if (len < 32)
      return -1;
    h264_private::VCL_NAL vcl;
    memset(&vcl, 0, sizeof(h264_private::VCL_NAL));
    vcl.nal_ref_idc = startcode & 0x60;
    vcl.nal_unit_type = startcode & 0x1F;
    if (!Parse_SLH(buf, len, vcl))
      return 0;

    // check for the beginning of a new access unit
    if (es_found_frame && IsFirstVclNal(vcl))
    {
      complete = true;
      es_consumed = buf_ptr - 4;
      return -1;
    }

    if (!es_found_frame)
    {
      if (buf_ptr - 4 >= (int)es_pts_pointer)
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
    break;
  }

  case NAL_SEI:
    if (es_found_frame)
    {
      complete = true;
      es_consumed = buf_ptr - 4;
      return -1;
    }
    break;

  case NAL_SPS:
  {
    if (es_found_frame)
    {
      complete = true;
      es_consumed = buf_ptr - 4;
      return -1;
    }
    // TODO: how big is SPS?
    if (len < 256)
      return -1;
    if (!Parse_SPS(buf, len))
      return 0;

    m_NeedSPS = false;
    break;
  }

  case NAL_PPS:
  {
    if (es_found_frame)
    {
      complete = true;
      es_consumed = buf_ptr - 4;
      return -1;
    }
    // TODO: how big is PPS
    if (len < 64)
      return -1;
    if (!Parse_PPS(buf, len))
      return 0;
    m_NeedPPS = false;
    break;
  }

  case NAL_AUD:
    if (es_found_frame && (p_pts != PTS_UNSET))
    {
      complete = true;
      es_consumed = buf_ptr - 4;
      return -1;
    }
    break;

  case NAL_END_SEQ:
    if (es_found_frame)
    {
      complete = true;
      es_consumed = buf_ptr;
      return -1;
    }
    break;

  case 13:
  case 14:
  case 15:
  case 16:
  case 17:
  case 18:
    if (es_found_frame)
    {
      complete = true;
      es_consumed = buf_ptr - 4;
      return -1;
    }
    break;

  default:
    break;
  }

  return 0;
}

bool ES_h264::Parse_PPS(uint8_t *buf, int len)
{
  CBitstream bs(buf, len*8);

  int pps_id = bs.readGolombUE();
  int sps_id = bs.readGolombUE();
  m_streamData.pps[pps_id].sps = sps_id;
  bs.readBits1();
  m_streamData.pps[pps_id].pic_order_present_flag = bs.readBits1();
  return true;
}

bool ES_h264::Parse_SLH(uint8_t *buf, int len, h264_private::VCL_NAL &vcl)
{
  CBitstream bs(buf, len*8);

  bs.readGolombUE(); /* first_mb_in_slice */
  int slice_type = bs.readGolombUE();

  if (slice_type > 4)
    slice_type -= 5;  /* Fixed slice type per frame */

  switch (slice_type)
  {
  case 0:
    break;
  case 1:
    break;
  case 2:
    m_NeedIFrame = false;
    break;
  default:
    return false;
  }

  int pps_id = bs.readGolombUE();
  int sps_id = m_streamData.pps[pps_id].sps;
  if (m_streamData.sps[sps_id].cbpsize == 0)
    return false;

  m_vbvSize = m_streamData.sps[sps_id].cbpsize;
  m_vbvDelay = -1;

  vcl.pic_parameter_set_id = pps_id;
  vcl.frame_num = bs.readBits(m_streamData.sps[sps_id].log2_max_frame_num);
  if (!m_streamData.sps[sps_id].frame_mbs_only_flag)
  {
    vcl.field_pic_flag = bs.readBits1();
    // interlaced
    if (vcl.field_pic_flag)
      m_Interlaced = true;
  }
  if (vcl.field_pic_flag)
    vcl.bottom_field_flag = bs.readBits1();

  if (vcl.nal_unit_type == 5)
    vcl.idr_pic_id = bs.readGolombUE();
  if (m_streamData.sps[sps_id].pic_order_cnt_type == 0)
  {
    vcl.pic_order_cnt_lsb = bs.readBits(m_streamData.sps[sps_id].log2_max_pic_order_cnt_lsb);
    if(m_streamData.pps[pps_id].pic_order_present_flag && !vcl.field_pic_flag)
      vcl.delta_pic_order_cnt_bottom = bs.readGolombSE();
  }
  if(m_streamData.sps[sps_id].pic_order_cnt_type == 1 &&
      !m_streamData.sps[sps_id].delta_pic_order_always_zero_flag )
  {
    vcl.delta_pic_order_cnt_0 = bs.readGolombSE();
    if(m_streamData.pps[pps_id].pic_order_present_flag && !vcl.field_pic_flag )
      vcl.delta_pic_order_cnt_1 = bs.readGolombSE();
  }

  vcl.pic_order_cnt_type = m_streamData.sps[sps_id].pic_order_cnt_type;

  return true;
}

bool ES_h264::Parse_SPS(uint8_t *buf, int len)
{
  CBitstream bs(buf, len*8);
  unsigned int tmp, frame_mbs_only;
  int cbpsize = -1;

  int profile_idc = bs.readBits(8);
  /* constraint_set0_flag = bs.readBits1();    */
  /* constraint_set1_flag = bs.readBits1();    */
  /* constraint_set2_flag = bs.readBits1();    */
  /* constraint_set3_flag = bs.readBits1();    */
  /* reserved             = bs.readBits(4);    */
  bs.skipBits(8);
  int level_idc = bs.readBits(8);
  unsigned int seq_parameter_set_id = bs.readGolombUE(9);

  unsigned int i = 0;
  while (h264_lev2cpbsize[i][0] != -1)
  {
    if (h264_lev2cpbsize[i][0] >= level_idc)
    {
      cbpsize = h264_lev2cpbsize[i][1];
      break;
    }
    i++;
  }
  if (cbpsize < 0)
    return false;

  memset(&m_streamData.sps[seq_parameter_set_id], 0, sizeof(h264_private::SPS));
  m_streamData.sps[seq_parameter_set_id].cbpsize = cbpsize * 125; /* Convert from kbit to bytes */

  if( profile_idc == 100 || profile_idc == 110 ||
      profile_idc == 122 || profile_idc == 244 || profile_idc == 44 ||
      profile_idc == 83 || profile_idc == 86 || profile_idc == 118 ||
      profile_idc == 128 )
  {
    int chroma_format_idc = bs.readGolombUE(9); /* chroma_format_idc              */
    if(chroma_format_idc == 3)
      bs.skipBits(1);           /* residual_colour_transform_flag */
    bs.readGolombUE();          /* bit_depth_luma - 8             */
    bs.readGolombUE();          /* bit_depth_chroma - 8           */
    bs.skipBits(1);             /* transform_bypass               */
    if (bs.readBits1())         /* seq_scaling_matrix_present     */
    {
      for (int i = 0; i < ((chroma_format_idc != 3) ? 8 : 12); i++)
      {
        if (bs.readBits1())     /* seq_scaling_list_present       */
        {
          int last = 8, next = 8, size = (i<6) ? 16 : 64;
          for (int j = 0; j < size; j++)
          {
            if (next)
              next = (last + bs.readGolombSE()) & 0xff;
            last = !next ? last: next;
          }
        }
      }
    }
  }

  int log2_max_frame_num_minus4 = bs.readGolombUE();           /* log2_max_frame_num - 4 */
  m_streamData.sps[seq_parameter_set_id].log2_max_frame_num = log2_max_frame_num_minus4 + 4;
  int pic_order_cnt_type = bs.readGolombUE(9);
  m_streamData.sps[seq_parameter_set_id].pic_order_cnt_type = pic_order_cnt_type;
  if (pic_order_cnt_type == 0)
  {
    int log2_max_pic_order_cnt_lsb_minus4 = bs.readGolombUE();         /* log2_max_poc_lsb - 4 */
    m_streamData.sps[seq_parameter_set_id].log2_max_pic_order_cnt_lsb = log2_max_pic_order_cnt_lsb_minus4 + 4;
  }
  else if (pic_order_cnt_type == 1)
  {
    m_streamData.sps[seq_parameter_set_id].delta_pic_order_always_zero_flag = bs.readBits1();
    bs.readGolombSE();         /* offset_for_non_ref_pic          */
    bs.readGolombSE();         /* offset_for_top_to_bottom_field  */
    tmp = bs.readGolombUE();   /* num_ref_frames_in_pic_order_cnt_cycle */
    for (unsigned int i = 0; i < tmp; i++)
      bs.readGolombSE();       /* offset_for_ref_frame[i]         */
  }
  else if(pic_order_cnt_type != 2)
  {
    /* Illegal poc */
    return false;
  }

  bs.readGolombUE(9);          /* ref_frames                      */
  bs.skipBits(1);             /* gaps_in_frame_num_allowed       */
  m_Width  /* mbs */ = bs.readGolombUE() + 1;
  m_Height /* mbs */ = bs.readGolombUE() + 1;
  frame_mbs_only     = bs.readBits1();
  m_streamData.sps[seq_parameter_set_id].frame_mbs_only_flag = frame_mbs_only;
  DBG(DEMUX_DBG_PARSE, "H.264 SPS: pic_width:  %u mbs\n", (unsigned) m_Width);
  DBG(DEMUX_DBG_PARSE, "H.264 SPS: pic_height: %u mbs\n", (unsigned) m_Height);
  DBG(DEMUX_DBG_PARSE, "H.264 SPS: frame only flag: %d\n", frame_mbs_only);

  m_Width  *= 16;
  m_Height *= 16 * (2-frame_mbs_only);

  if (!frame_mbs_only)
  {
    if (bs.readBits1())     /* mb_adaptive_frame_field_flag */
      DBG(DEMUX_DBG_PARSE, "H.264 SPS: MBAFF\n");
  }
  bs.skipBits(1);           /* direct_8x8_inference_flag    */
  if (bs.readBits1())       /* frame_cropping_flag */
  {
    uint32_t crop_left   = bs.readGolombUE();
    uint32_t crop_right  = bs.readGolombUE();
    uint32_t crop_top    = bs.readGolombUE();
    uint32_t crop_bottom = bs.readGolombUE();
    DBG(DEMUX_DBG_PARSE, "H.264 SPS: cropping %d %d %d %d\n", crop_left, crop_top, crop_right, crop_bottom);

    m_Width -= 2*(crop_left + crop_right);
    if (frame_mbs_only)
      m_Height -= 2*(crop_top + crop_bottom);
    else
      m_Height -= 4*(crop_top + crop_bottom);
  }

  /* VUI parameters */
  m_PixelAspect.num = 0;
  if (bs.readBits1())    /* vui_parameters_present flag */
  {
    if (bs.readBits1())  /* aspect_ratio_info_present */
    {
      uint32_t aspect_ratio_idc = bs.readBits(8);
      DBG(DEMUX_DBG_PARSE, "H.264 SPS: aspect_ratio_idc %d\n", aspect_ratio_idc);

      if (aspect_ratio_idc == 255 /* Extended_SAR */)
      {
        m_PixelAspect.num = bs.readBits(16); /* sar_width */
        m_PixelAspect.den = bs.readBits(16); /* sar_height */
        DBG(DEMUX_DBG_PARSE, "H.264 SPS: -> sar %dx%d\n", m_PixelAspect.num, m_PixelAspect.den);
      }
      else
      {
        static const mpeg_rational_t aspect_ratios[] =
        { /* page 213: */
          /* 0: unknown */
          {0, 1},
          /* 1...16: */
          { 1,  1}, {12, 11}, {10, 11}, {16, 11}, { 40, 33}, {24, 11}, {20, 11}, {32, 11},
          {80, 33}, {18, 11}, {15, 11}, {64, 33}, {160, 99}, { 4,  3}, { 3,  2}, { 2,  1}
        };

        if (aspect_ratio_idc < sizeof(aspect_ratios)/sizeof(aspect_ratios[0]))
        {
          memcpy(&m_PixelAspect, &aspect_ratios[aspect_ratio_idc], sizeof(mpeg_rational_t));
          DBG(DEMUX_DBG_PARSE, "H.264 SPS: PAR %d / %d\n", m_PixelAspect.num, m_PixelAspect.den);
        }
        else
        {
          DBG(DEMUX_DBG_PARSE, "H.264 SPS: aspect_ratio_idc out of range !\n");
        }
      }
    }
    if (bs.readBits1()) // overscan
    {
      bs.readBits1(); // overscan_appropriate_flag
    }
    if (bs.readBits1()) // video_signal_type_present_flag
    {
      bs.readBits(3); // video_format
      bs.readBits1(); // video_full_range_flag
      if (bs.readBits1()) // colour_description_present_flag
      {
        bs.readBits(8); // colour_primaries
        bs.readBits(8); // transfer_characteristics
        bs.readBits(8); // matrix_coefficients
      }
    }

    if (bs.readBits1()) // chroma_loc_info_present_flag
    {
      bs.readGolombUE(); // chroma_sample_loc_type_top_field
      bs.readGolombUE(); // chroma_sample_loc_type_bottom_field
    }

    if (bs.readBits1()) // timing_info_present_flag
    {
//      uint32_t num_units_in_tick = bs.readBits(32);
//      uint32_t time_scale = bs.readBits(32);
//      int fixed_frame_rate = bs.readBits1();
//      if (num_units_in_tick > 0)
//        m_FPS = time_scale / (num_units_in_tick * 2);
    }
  }

  DBG(DEMUX_DBG_PARSE, "H.264 SPS: -> video size %dx%d, aspect %d:%d\n", m_Width, m_Height, m_PixelAspect.num, m_PixelAspect.den);
  return true;
}

bool ES_h264::IsFirstVclNal(h264_private::VCL_NAL &vcl)
{
  if (m_streamData.vcl_nal.frame_num != vcl.frame_num)
    return true;

  if (m_streamData.vcl_nal.pic_parameter_set_id != vcl.pic_parameter_set_id)
    return true;

  if (m_streamData.vcl_nal.field_pic_flag != vcl.field_pic_flag)
    return true;

  if (m_streamData.vcl_nal.field_pic_flag && vcl.field_pic_flag)
  {
    if (m_streamData.vcl_nal.bottom_field_flag != vcl.bottom_field_flag)
      return true;
  }

  if (m_streamData.vcl_nal.nal_ref_idc == 0 || vcl.nal_ref_idc == 0)
  {
    if (m_streamData.vcl_nal.nal_ref_idc != vcl.nal_ref_idc)
      return true;
  }

  if (m_streamData.vcl_nal.pic_order_cnt_type == 0 && vcl.pic_order_cnt_type == 0)
  {
    if (m_streamData.vcl_nal.pic_order_cnt_lsb != vcl.pic_order_cnt_lsb)
      return true;
    if (m_streamData.vcl_nal.delta_pic_order_cnt_bottom != vcl.delta_pic_order_cnt_bottom)
      return true;
  }

  if (m_streamData.vcl_nal.pic_order_cnt_type == 1 && vcl.pic_order_cnt_type == 1)
  {
    if (m_streamData.vcl_nal.delta_pic_order_cnt_0 != vcl.delta_pic_order_cnt_0)
      return true;
    if (m_streamData.vcl_nal.delta_pic_order_cnt_1 != vcl.delta_pic_order_cnt_1)
      return true;
  }

  if (m_streamData.vcl_nal.nal_unit_type == 5 || vcl.nal_unit_type == 5)
  {
    if (m_streamData.vcl_nal.nal_unit_type != vcl.nal_unit_type)
      return true;
  }

  if (m_streamData.vcl_nal.nal_unit_type == 5 && vcl.nal_unit_type == 5)
  {
    if (m_streamData.vcl_nal.idr_pic_id != vcl.idr_pic_id)
      return true;
  }
  return false;
}
