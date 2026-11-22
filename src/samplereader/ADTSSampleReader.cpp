/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ADTSSampleReader.h"

#include "AdaptiveByteStream.h"

CADTSSampleReader::CADTSSampleReader(AP4_ByteStream* input)
  : ADTSReader{input}, m_adByteStream{dynamic_cast<CAdaptiveByteStream*>(input)}
{
}

bool CADTSSampleReader::Initialize(SESSION::CStream* stream)
{
  // This is a workaround to avoid start buffering many segments
  // when the sample reader is initialized just to retrieve the stream info
  m_adByteStream->AllowBufferQueue(false);
  ADTSReader::FetchStreamInfo();
  m_adByteStream->AllowBufferQueue(true);
  return true;
}

AP4_Result CADTSSampleReader::Start(std::optional<uint64_t> pts, PtsType ptsType)
{
  if (m_started)
    return AP4_SUCCESS;

  AP4_Result ret;
  if (pts.has_value())
    TimeSeek(*pts, ptsType) ? ret = AP4_SUCCESS : ret = AP4_ERROR_EOS;
  else
    ret = ReadSample();

  m_started = AP4_SUCCEEDED(ret);
  return ret;
}

AP4_Result CADTSSampleReader::ReadSample()
{
  if (ReadPacket())
  {
    UpdatePts();
    return AP4_SUCCESS;
  }

  if (m_adByteStream->waitingForSegment())
  {
    // if waiting for segment, we will return success to avoid EOS and let the caller to call ReadSample again
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

bool CADTSSampleReader::TimeSeek(uint64_t pts, PtsType ptsType)
{
  // ADTSReader::SeekTime expect a sample already read to be able to find the correct position of the requested PTS
  if (AP4_FAILED(ReadSample()))
    return false;

  // compensate the pts diff with the manifest timing, the sample read above may have refreshed it
  if (ptsType == PtsType::MANIFEST)
    pts += m_ptsDiff;

  AP4_UI64 seekPos{(pts * 9) / 100};
  if (ADTSReader::SeekTime(seekPos))
  {
    m_started = true;
    // ADTSReader::SeekTime may have read other packages to find the requested PTS, so update the PTS/DTS values
    UpdatePts();
    return true;
  }
  return false; // EOS
}

void CADTSSampleReader::UpdatePts()
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
}
