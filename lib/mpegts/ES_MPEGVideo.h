/*
 *  Copyright (C) 2005-2013 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#ifndef ES_MPEGVIDEO_H
#define ES_MPEGVIDEO_H

#include "elementaryStream.h"

namespace TSDemux
{
  class ES_MPEG2Video : public ElementaryStream
  {
  private:
    uint32_t        m_StartCode;
    bool            m_NeedIFrame;
    bool            m_NeedSPS;
    int             m_FrameDuration;
    int             m_vbvDelay;       /* -1 if CBR */
    int             m_vbvSize;        /* Video buffer size (in bytes) */
    int             m_Width;
    int             m_Height;
    float           m_Dar;
    int64_t         m_DTS;
    int64_t         m_PTS;
    int64_t         m_AuDTS, m_AuPTS, m_AuPrevDTS;
    int             m_TemporalReference;
    int             m_TrLastTime;
    int             m_PicNumber;
    int             m_FpsScale;

    int Parse_MPEG2Video(uint32_t startcode, int buf_ptr, bool &complete);
    bool Parse_MPEG2Video_SeqStart(uint8_t *buf);
    bool Parse_MPEG2Video_PicStart(uint8_t *buf);

  public:
    ES_MPEG2Video(uint16_t pid);
    virtual ~ES_MPEG2Video();

    virtual void Parse(STREAM_PKT* pkt);
    virtual void Reset();
  };
}

#endif /* ES_MPEGVIDEO_H */
