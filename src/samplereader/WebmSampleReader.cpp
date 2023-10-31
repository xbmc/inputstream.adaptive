/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WebmSampleReader.h"

#include "AdaptiveByteStream.h"

CWebmSampleReader::CWebmSampleReader(AP4_ByteStream* input, AP4_UI32 streamId)
  : WebmReader{input},
    m_streamId{streamId},
    m_adByteStream{dynamic_cast<CAdaptiveByteStream*>(input)} {};

bool CWebmSampleReader::Initialize()
{
  m_adByteStream->FixateInitialization(true);
  bool ret = WebmReader::Initialize();
  WebmReader::Reset();
  m_adByteStream->FixateInitialization(false);
  m_adByteStream->SetSegmentFileOffset(GetCueOffset());
  return ret;
}

AP4_Result CWebmSampleReader::Start(bool& bStarted)
{
  bStarted = false;
  if (m_started)
    return AP4_SUCCESS;
  m_started = bStarted = true;
  return ReadSample();
}

AP4_Result CWebmSampleReader::ReadSample()
{
  if (ReadPacket())
  {
    m_dts = GetDts() * 1000;
    m_pts = GetPts() * 1000;

    if (~m_ptsOffs)
    {
      m_ptsDiff = m_pts - m_ptsOffs;
      m_ptsOffs = ~0ULL;
    }
    return AP4_SUCCESS;
  }
  if (!m_adByteStream || !m_adByteStream->waitingForSegment())
    m_eos = true;
  return AP4_ERROR_EOS;
}

void CWebmSampleReader::Reset(bool bEOS)
{
  WebmReader::Reset();
  m_eos = bEOS;
}

bool CWebmSampleReader::TimeSeek(uint64_t pts, bool preceeding)
{
  AP4_UI64 seekPos((pts * 9) / 100);
  if (WebmReader::SeekTime(seekPos, preceeding))
  {
    m_started = true;
    return AP4_SUCCEEDED(ReadSample());
  }
  return AP4_ERROR_EOS;
}
