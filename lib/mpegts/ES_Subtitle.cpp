/*
 *  Copyright (C) 2005-2013 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ES_Subtitle.h"

using namespace TSDemux;

ES_Subtitle::ES_Subtitle(uint16_t pid)
 : ElementaryStream(pid)
{
  es_alloc_init = 4000;
  has_stream_info = true; // doesn't provide stream info
}

ES_Subtitle::~ES_Subtitle()
{

}

void ES_Subtitle::Parse(STREAM_PKT* pkt)
{
  int l = es_len - es_parsed;

  if (l > 0)
  {
    if (l < 2 || es_buf[0] != 0x20 || es_buf[1] != 0x00)
    {
      Reset();
      return;
    }

    if(es_buf[l-1] == 0xff)
    {
      pkt->pid          = pid;
      pkt->data         = es_buf+2;
      pkt->size         = l-3;
      pkt->duration     = 0;
      pkt->dts          = c_dts;
      pkt->pts          = c_pts;
      pkt->streamChange = false;
    }

    es_parsed = es_consumed = es_len;
  }
}
