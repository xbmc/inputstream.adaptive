/*
 *  Copyright (C) 2005-2013 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ES_Teletext.h"

using namespace TSDemux;

ES_Teletext::ES_Teletext(uint16_t pid)
 : ElementaryStream(pid)
{
  es_alloc_init = 4000;
  has_stream_info = true; // doesn't provide stream info
}

ES_Teletext::~ES_Teletext()
{
}

void ES_Teletext::Parse(STREAM_PKT* pkt)
{
  int l = es_len - es_parsed;
  if (l < 1)
    return;

  if (es_buf[0] < 0x10 || es_buf[0] > 0x1F)
  {
    Reset();
    return;
  }

  pkt->pid          = pid;
  pkt->data         = es_buf;
  pkt->size         = l;
  pkt->duration     = 0;
  pkt->dts          = c_dts;
  pkt->pts          = c_pts;
  pkt->streamChange = false;

  es_parsed = es_consumed = es_len;
}
