/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ADTSSampleReader.h"

#include "AdaptiveByteStream.h"

CADTSSampleReader::CADTSSampleReader(AP4_ByteStream* input, AP4_UI32 streamId)
  : ADTSReader{input},
    m_streamId{streamId},
    m_adByteStream{dynamic_cast<CAdaptiveByteStream*>(input)}
{
}

AP4_Result CADTSSampleReader::Start(bool& bStarted)
{
  bStarted = false;
  if (m_started)
    return AP4_SUCCESS;
  bStarted = true;
  m_started = true;
  return ReadSample();
}

AP4_Result CADTSSampleReader::ReadSample()
{
  if (ReadPacket())
  {
    uint64_t pts{GetPts()};
    if (pts == ADTSReader::ADTS_PTS_UNSET)
      m_pts = STREAM_NOPTS_VALUE;
    else
      m_pts = (pts * 100) / 9;

    if (~m_ptsOffs)
    {
      m_ptsDiff = m_pts - m_ptsOffs;
      m_ptsOffs = ~0ULL;
    }
    return AP4_SUCCESS;
  }
  if (!m_adByteStream || !m_adByteStream->waitingForSegment())
  {
    m_eos = true;
  }
  return AP4_ERROR_EOS;
}

void CADTSSampleReader::Reset(bool bEOS)
{
  ADTSReader::Reset();
  m_eos = bEOS;
}

bool CADTSSampleReader::TimeSeek(uint64_t pts, bool preceeding)
{
  AP4_UI64 seekPos{(pts * 9) / 100};
  if (ADTSReader::SeekTime(seekPos, preceeding))
  {
    m_started = true;
    return AP4_SUCCEEDED(ReadSample());
  }
  return AP4_ERROR_EOS;
}
