/*
 *      Copyright (C) 2013 Jean-Luc Barriere
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
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301 USA
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "elementaryStream.h"
#include "debug.h"

#include <cstdlib>    // for malloc free size_t
#include <cstring>    // memset memcpy memmove
#include <climits>    // for INT_MAX
#include <cerrno>

using namespace TSDemux;

ElementaryStream::ElementaryStream(uint16_t pes_pid)
  : pid(pes_pid)
  , stream_type(STREAM_TYPE_UNKNOWN)
  , c_dts(PTS_UNSET)
  , c_pts(PTS_UNSET)
  , p_dts(PTS_UNSET)
  , p_pts(PTS_UNSET)
  , has_stream_info(false)
  , es_alloc_init(ES_INIT_BUFFER_SIZE)
  , es_buf(NULL)
  , es_alloc(0)
  , es_len(0)
  , es_consumed(0)
  , es_pts_pointer(0)
  , es_parsed(0)
  , es_found_frame(false)
  , es_frame_valid(false)
  , es_extraDataChanged(false)
{
  memset(&stream_info, 0, sizeof(STREAM_INFO));
}

ElementaryStream::~ElementaryStream(void)
{
  if (es_buf)
  {
    DBG(DEMUX_DBG_DEBUG, "free stream buffer %.4x: allocated size was %zu\n", pid, es_alloc);
    free(es_buf);
    es_buf = NULL;
  }
}

void ElementaryStream::Reset(void)
{
  ClearBuffer();
  es_found_frame = false;
  es_frame_valid = false;
}

void ElementaryStream::ClearBuffer()
{
  es_len = es_consumed = es_pts_pointer = es_parsed = 0;
}

int ElementaryStream::Append(const unsigned char* buf, size_t len, bool new_pts)
{
  // Mark position where current pts become applicable
  if (new_pts)
    es_pts_pointer = es_len;

  if (es_buf && es_consumed)
  {
    if (es_consumed < es_len)
    {
      memmove(es_buf, es_buf + es_consumed, es_len - es_consumed);
      es_len -= es_consumed;
      es_parsed -= es_consumed;
      if (es_pts_pointer > es_consumed)
        es_pts_pointer -= es_consumed;
      else
        es_pts_pointer = 0;

      es_consumed = 0;
    }
    else
      ClearBuffer();
  }
  if (es_len + len > es_alloc)
  {
    if (es_alloc >= ES_MAX_BUFFER_SIZE)
      return -ENOMEM;

    size_t n = (es_alloc ? (es_alloc + len) * 2 : es_alloc_init);
    if (n > ES_MAX_BUFFER_SIZE)
      n = ES_MAX_BUFFER_SIZE;

    DBG(DEMUX_DBG_DEBUG, "realloc buffer size to %zu for stream %.4x\n", n, pid);
    unsigned char* p = es_buf;
    es_buf = (unsigned char*)realloc(es_buf, n * sizeof(*es_buf));
    if (es_buf)
    {
      es_alloc = n;
    }
    else
    {
      free(p);
      es_alloc = 0;
      es_len = 0;
      return -ENOMEM;
    }
  }

  if (!es_buf)
    return -ENOMEM;

  memcpy(es_buf + es_len, buf, len);
  es_len += len;

  return 0;
}

const char* ElementaryStream::GetStreamCodecName(STREAM_TYPE stream_type)
{
  switch (stream_type)
  {
    case STREAM_TYPE_VIDEO_MPEG1:
      return "mpeg1video";
    case STREAM_TYPE_VIDEO_MPEG2:
      return "mpeg2video";
    case STREAM_TYPE_AUDIO_MPEG1:
      return "mp1";
    case STREAM_TYPE_AUDIO_MPEG2:
      return "mp2";
    case STREAM_TYPE_AUDIO_AAC:
      return "aac";
    case STREAM_TYPE_AUDIO_AAC_ADTS:
      return "aac";
    case STREAM_TYPE_AUDIO_AAC_LATM:
      return "aac_latm";
    case STREAM_TYPE_VIDEO_H264:
      return "h264";
    case STREAM_TYPE_VIDEO_HEVC:
      return "hevc";
    case STREAM_TYPE_AUDIO_AC3:
      return "ac3";
    case STREAM_TYPE_AUDIO_EAC3:
      return "eac3";
    case STREAM_TYPE_DVB_TELETEXT:
      return "teletext";
    case STREAM_TYPE_DVB_SUBTITLE:
      return "dvbsub";
    case STREAM_TYPE_VIDEO_MPEG4:
      return "mpeg4video";
    case STREAM_TYPE_VIDEO_VC1:
      return "vc1";
    case STREAM_TYPE_AUDIO_LPCM:
      return "lpcm";
    case STREAM_TYPE_AUDIO_DTS:
      return "dts";
    case STREAM_TYPE_PRIVATE_DATA:
    default:
      return "data";
  }
}

const char* ElementaryStream::GetStreamCodecName() const
{
  return GetStreamCodecName(stream_type);
}

bool ElementaryStream::GetStreamPacket(STREAM_PKT* pkt)
{
  ResetStreamPacket(pkt);
  Parse(pkt);
  if (pkt->data)
    return true;
  return false;
}

void ElementaryStream::Parse(STREAM_PKT* pkt)
{
  // No parser: pass-through
  if (es_consumed < es_len)
  {
    es_consumed = es_parsed = es_len;
    pkt->pid              = pid;
    pkt->size             = es_consumed;
    pkt->data             = es_buf;
    pkt->dts              = c_dts;
    pkt->pts              = c_pts;
    if (c_dts == PTS_UNSET || p_dts == PTS_UNSET)
      pkt->duration       = 0;
    else
      pkt->duration       = c_dts - p_dts;
    pkt->streamChange     = false;
  }
}

void ElementaryStream::ResetStreamPacket(STREAM_PKT* pkt)
{
  pkt->pid                = 0xffff;
  pkt->size               = 0;
  pkt->data               = NULL;
  pkt->dts                = PTS_UNSET;
  pkt->pts                = PTS_UNSET;
  pkt->duration           = 0;
  pkt->streamChange       = false;
  pkt->recoveryPoint      = false;
}

uint64_t ElementaryStream::Rescale(uint64_t a, uint64_t b, uint64_t c)
{
  uint64_t r = c / 2;

  if (b <= INT_MAX && c <= INT_MAX)
  {
    if (a <= INT_MAX)
      return (a * b + r) / c;
    else
      return a / c * b + (a % c * b + r) / c;
  }
  else
  {
    uint64_t a0 = a & 0xFFFFFFFF;
    uint64_t a1 = a >> 32;
    uint64_t b0 = b & 0xFFFFFFFF;
    uint64_t b1 = b >> 32;
    uint64_t t1 = a0 * b1 + a1 * b0;
    uint64_t t1a = t1 << 32;

    a0 = a0 * b0 + t1a;
    a1 = a1 * b1 + (t1 >> 32) + (a0 < t1a);
    a0 += r;
    a1 += a0 < r;

    for (int i = 63; i >= 0; i--)
    {
      a1 += a1 + ((a0 >> i) & 1);
      t1 += t1;
      if (c <= a1)
      {
        a1 -= c;
        t1++;
      }
    }
    return t1;
  }
}

bool ElementaryStream::SetVideoInformation(int FpsScale, int FpsRate, int Height, int Width, float Aspect, bool Interlaced)
{
  bool ret = false;
  if ((stream_info.fps_scale != FpsScale) ||
      (stream_info.fps_rate != FpsRate) ||
      (stream_info.height != Height) ||
      (stream_info.width != Width) ||
      (stream_info.aspect != Aspect) ||
      (stream_info.interlaced != Interlaced))
    ret = true;

  stream_info.fps_scale       = FpsScale;
  stream_info.fps_rate        = FpsRate;
  stream_info.height          = Height;
  stream_info.width           = Width;
  stream_info.aspect          = Aspect;
  stream_info.interlaced      = Interlaced;

  has_stream_info = true;
  return ret;
}

bool ElementaryStream::SetAudioInformation(int Channels, int SampleRate, int BitRate, int BitsPerSample, int BlockAlign)
{
  bool ret = false;
  if ((stream_info.channels != Channels) ||
      (stream_info.sample_rate != SampleRate) ||
      (stream_info.block_align != BlockAlign) ||
      (stream_info.bit_rate != BitRate) ||
      (stream_info.bits_per_sample != BitsPerSample))
    ret = true;

  stream_info.channels          = Channels;
  stream_info.sample_rate       = SampleRate;
  stream_info.block_align       = BlockAlign;
  stream_info.bit_rate          = BitRate;
  stream_info.bits_per_sample   = BitsPerSample;

  has_stream_info = true;
  return ret;
}
