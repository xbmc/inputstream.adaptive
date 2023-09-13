/*
 *  Copyright (C) 2005-2015 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#ifndef ES_HEVC_H
#define ES_HEVC_H

#include "elementaryStream.h"

namespace TSDemux
{
  class ES_hevc : public ElementaryStream
  {
  private:
    typedef struct hevc_private
    {
      struct PPS
      {
        int sps;
        int dependent_slice_segments_enabled_flag;
      } pps[64];

      struct VCL_NAL
      {
        int pic_parameter_set_id; // slice
        unsigned int first_slice_segment_in_pic_flag;
        unsigned int nal_unit_type;
      } vcl_nal;

    } hevc_private_t;

    typedef struct HDR_NAL_t
    {
       unsigned nal_unit_type;
       unsigned nuh_layer_id;
       unsigned nuh_temporal_id;
    } HDR_NAL;

    typedef struct mpeg_rational_s {
      int num;
      int den;
    } mpeg_rational_t;

    enum
    {
      NAL_TRAIL_N  = 0x00, // Coded slice segment of trailing picture
      NAL_TRAIL_R  = 0x01, // Coded slice segment of trailing picture
      NAL_TSA_N    = 0x02, // Coded slice segment of TSA picture
      NAL_TSA_R    = 0x03, // Coded slice segment of TSA picture
      NAL_STSA_N   = 0x04, // Coded slice segment of STSA picture
      NAL_STSA_R   = 0x05, // Coded slice segment of STSA picture
      NAL_RADL_N   = 0x06, // Coded slice segment of RADL picture
      NAL_RADL_R   = 0x07, // Coded slice segment of RADL picture
      NAL_RASL_N   = 0x08, // Coded slice segment of RASL picture
      NAL_RASL_R   = 0x09, // Coded slice segment of RASL picture

      NAL_BLA_W_LP = 0x10, // Coded slice segment of a BLA picture
      NAL_CRA_NUT  = 0x15, // Coded slice segment of a CRA picture
      NAL_RSV_IRAP_VCL22 = 0x16, // Reserved IRAP VCL NAL unit types
      NAL_RSV_IRAP_VCL23 = 0x17, // Reserved IRAP VCL NAL unit types

      NAL_VPS_NUT  = 0x20, // Video Parameter SET
      NAL_SPS_NUT  = 0x21, // Sequence Parameter Set
      NAL_PPS_NUT  = 0x22, // Picture Parameter Set
      NAL_AUD_NUT  = 0x23, // Access Unit Delimiter
      NAL_EOS_NUT  = 0x24, // End of Sequence
      NAL_EOB_NUT  = 0x25, // End of Bitstream
      NAL_FD_NUT   = 0x26, // Filler Data
      NAL_PFX_SEI_NUT  = 0x27, // Supplemental Enhancement Information
      NAL_SFX_SEI_NUT  = 0x28, // Supplemental Enhancement Information

    };

    uint32_t        m_StartCode;
    int             m_LastStartPos;
    bool            m_NeedVPS;
    bool            m_NeedSPS;
    bool            m_NeedPPS;
    int             m_Width;
    int             m_Height;
    int             m_FpsScale;
    mpeg_rational_t m_PixelAspect;
    hevc_private_t  m_streamData;
    int64_t         m_DTS;
    int64_t         m_PTS;
    bool            m_Interlaced;

    void Parse_HEVC(int buf_ptr, unsigned int NumBytesInNalUnit, bool &complete);
    void Parse_PPS(uint8_t *buf, int len);
    void Parse_SLH(uint8_t *buf, int len, HDR_NAL hdr, hevc_private::VCL_NAL &vcl);
    void Parse_SPS(uint8_t *buf, int len, HDR_NAL hdr);
    bool IsFirstVclNal(hevc_private::VCL_NAL &vcl);

  public:
    ES_hevc(uint16_t pes_pid);
    virtual ~ES_hevc();

    virtual void Parse(STREAM_PKT* pkt);
    virtual void Reset();
  };

}

#endif /* ES_HEVC_H */

