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
