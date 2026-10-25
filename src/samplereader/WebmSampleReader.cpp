/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WebmSampleReader.h"

#include "AdaptiveByteStream.h"

CWebmSampleReader::CWebmSampleReader(AP4_ByteStream* input)
  : WebmReader{input}, m_adByteStream{dynamic_cast<CAdaptiveByteStream*>(input)} {};

bool CWebmSampleReader::Initialize(SESSION::CStream* stream)
{
  // The current demuxer implementation cannot determine the EOF when parsing
  // the initialization segment, so you need to force stop the read operation
  m_adByteStream->FixateInitialization(true);

  bool ret = WebmReader::Initialize();
  WebmReader::Reset();
  m_adByteStream->SetSegmentFileOffset(GetCueOffset());
  return ret;
}

AP4_Result CWebmSampleReader::Start(std::optional<uint64_t> pts)
{
  if (m_started)
    return AP4_SUCCESS;

  AP4_Result ret;
  if (pts.has_value())
    TimeSeek(*pts) ? ret = AP4_SUCCESS : ret = AP4_ERROR_EOS;
  else
    ret = ReadSample();

  m_started = AP4_SUCCEEDED(ret);
  return ret;
}

AP4_Result CWebmSampleReader::ReadSample()
{
  if (ReadPacket())
  {
    SetTimestamps();
    return AP4_SUCCESS;
  }

  if (m_adByteStream->waitingForSegment())
  {
    // if waiting for segment, we will return success to avoid EOS and let the caller to call ReadSample again
    return AP4_SUCCESS;
  }

  if (!m_adByteStream || !m_adByteStream->waitingForSegment())
    m_eos = true;
  return AP4_ERROR_EOS;
}

void CWebmSampleReader::SetTimestamps()
{
  m_dts = GetDts() * 1000;
  m_pts = GetPts() * 1000;

  if (~m_ptsOffs)
  {
    m_ptsDiff = m_pts - m_ptsOffs;
    m_ptsOffs = ~0ULL;
  }
}

void CWebmSampleReader::Reset(bool bEOS)
{
  WebmReader::Reset();
  m_eos = bEOS;
}

bool CWebmSampleReader::TimeSeek(uint64_t pts)
{
  // compensate the pts diff with the manifest timing
  AP4_UI64 seekPos(((pts + m_ptsDiff) * 9) / 100);
  if (WebmReader::SeekTime(seekPos))
  {
    m_started = true;
    if (AP4_FAILED(ReadSample()))
      return false;

    // Reading resumes at the start of the segment, a key frame for video. Audio may start
    // anywhere, so its samples before the seek time are skipped to start along with the video.
    // The first sample read may only now have set the pts diff.
    const uint64_t seekPts{pts + m_ptsDiff};
    // Where no further sample can be read yet, the sample read last starts playback a little
    // early. The reader cannot tell this apart from the end of the stream here, and ending the
    // stream would silence it for the rest of the playback.
    while (!IsVideo() && m_pts < seekPts && ReadPacket())
      SetTimestamps();

    return true;
  }
  return false; // EOS
}
