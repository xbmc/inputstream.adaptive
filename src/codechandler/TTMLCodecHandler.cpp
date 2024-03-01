/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TTMLCodecHandler.h"

#include "CompResources.h"
#include "SrvBroker.h"
#include "common/AdaptiveTree.h"

// Kodi dont support TTML, so TTML is converted into "Text Subtitle Decoder" Kodi format
// managed with Kodi core overlay decoder "CDVDOverlayCodecText".
// This format must contain only subtitle text formatted as STR (SubRip) format in the packet data.
// 
// The Kodi overlay decoder can handle only one single subtitle (packet) at time.
// For multiple subtitles with equal/similar PTS start, you will have to send the packets individually,
// sequentially, at each next Kodi CInputStreamAdaptive::DemuxRead callback (routed to ReadNextSample).

bool TTMLCodecHandler::ReadNextSample(AP4_Sample& sample, AP4_DataBuffer& buf)
{
  uint64_t pts;
  uint32_t dur;

  if (m_ttml.Prepare(pts, dur))
  {
    buf.SetData(reinterpret_cast<const AP4_Byte*>(m_ttml.GetPreparedData()),
                static_cast<const AP4_Size>(m_ttml.GetPreparedDataSize()));
    sample.SetDts(pts + m_ptsOffset);
    sample.SetCtsDelta(0);
    sample.SetDuration(dur);
    return true;
  }
  else
    buf.SetDataSize(0);
  return false;
}

void TTMLCodecHandler::SetPTSOffset(AP4_UI64 offset)
{
  if (m_isTimeRelative)
    m_ptsOffset = offset;
}

TTMLCodecHandler::TTMLCodecHandler(AP4_SampleDescription* sd, bool isFile)
  : CodecHandler(sd), m_ttml(isFile)
{
  // For Miscrosoft Smooth Streaming, the subtitle time is relative to sample time,
  // in this case its needed apply an offset.
  m_isTimeRelative = CSrvBroker::GetResources().GetTree().IsTTMLTimeRelative();
}

bool TTMLCodecHandler::Transform(AP4_UI64 pts,
                                 AP4_UI32 duration,
                                 AP4_DataBuffer& buf,
                                 AP4_UI64 timescale)
{
  return m_ttml.Parse(buf.GetData(), buf.GetDataSize(), timescale);
}
