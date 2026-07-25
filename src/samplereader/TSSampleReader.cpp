/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TSSampleReader.h"

#include "AdaptiveByteStream.h"

CTSSampleReader::CTSSampleReader(AP4_ByteStream* input, uint32_t requiredMask)
  : TSReader{input, requiredMask}, m_adByteStream{dynamic_cast<CAdaptiveByteStream*>(input)}
{
}

void CTSSampleReader::SetStreamId(INPUTSTREAM_TYPE type, int streamId)
{
  m_typeMask = 1U << type;
  m_typeMap[INPUTSTREAM_TYPE_NONE] = streamId;
  m_typeMap[type] = streamId;
}

void CTSSampleReader::UpdatePts()
{
  //! @todo: there is something wrong on pts calculation,
  //! m_ptsOffs have a value in seconds and so the substraction "m_pts - m_ptsOffs" looks to be inconsistent,
  //! To have pts in seconds on m_pts must be: pts = GetDts() / 90000,
  //! but packet PTS seem to be different from m_ptsOffs pts value, as if it did not include the period start
  //! so the substraction "m_pts - m_ptsOffs" it is not a clear thing.
  //! There is also something weird on HLS discontinuities (multiple chapters/periods)
  //! where after a discontinuity the packet pts is lower than the last segment of previous period/discontinuity
  //! this cause a VP resync e.g:
  //!   debug <general>: CVideoPlayer::CheckContinuity - resync backward :1, prev:587175999.000000, curr:577170666.000000, diff:-10005333.000000
  //!   debug <general>: CVideoPlayer::CheckContinuity - update correction: -10026666.000000
  //! not sure if this is correct or not (tested with pluto-tv)
  //! This code is present also on the others sample readers, that need to be verified
  m_dts = (GetDts() == PTS_UNSET) ? STREAM_NOPTS_VALUE : (GetDts() * 100) / 9;
  m_pts = (GetPts() == PTS_UNSET) ? STREAM_NOPTS_VALUE : (GetPts() * 100) / 9;

  if (~m_ptsOffs)
  {
    m_ptsDiff = m_pts - m_ptsOffs;
    m_ptsOffs = ~0ULL;
  }
}

bool CTSSampleReader::Initialize(SESSION::CStream* stream)
{
  // This is a workaround to avoid start buffering many segments
  // when the sample reader is initialized just to retrieve the stream info
  m_adByteStream->AllowBufferQueue(false);
  bool ret = TSReader::Initialize();
  m_adByteStream->AllowBufferQueue(true);
  return ret;
}

void CTSSampleReader::AddStreamType(INPUTSTREAM_TYPE type, int streamId)
{
  m_typeMap[type] = streamId;
  m_typeMask |= (1 << type);
  if (m_started)
    StartStreaming(m_typeMask);
}

void CTSSampleReader::SetStreamType(INPUTSTREAM_TYPE type, int streamId)
{
  m_typeMap[type] = streamId;
  m_typeMask = (1 << type);
}

bool CTSSampleReader::RemoveStreamType(INPUTSTREAM_TYPE type)
{
  m_typeMask &= ~(1 << type);
  StartStreaming(m_typeMask);
  return m_typeMask == 0;
}

AP4_Result CTSSampleReader::Start(std::optional<uint64_t> pts)
{
  if (m_started)
    return AP4_SUCCESS;

  if (!StartStreaming(m_typeMask))
  {
    m_eos = true;
    return AP4_ERROR_CANNOT_OPEN_FILE;
  }

  AP4_Result ret;
  if (pts.has_value())
    TimeSeek(*pts) ? ret = AP4_SUCCESS : ret = AP4_ERROR_EOS;
  else
    ret = ReadSample();

  m_started = AP4_SUCCEEDED(ret);
  return ret;
}

AP4_Result CTSSampleReader::ReadSample()
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
    m_eos = true;
  return AP4_ERROR_EOS;
}

void CTSSampleReader::Reset(bool bEOS)
{
  TSReader::Reset();
  m_eos = bEOS;
}

bool CTSSampleReader::TimeSeek(uint64_t pts)
{
  return SeekToPts(pts, true);
}

bool CTSSampleReader::TimeSeekReaderPts(uint64_t pts)
{
  // The sample read in SeekToPts can refresh m_ptsDiff, so do not compensate it
  return SeekToPts(pts, false);
}

bool CTSSampleReader::SeekToPts(uint64_t pts, bool isManifestPts)
{
  if (!StartStreaming(m_typeMask))
    return false;

  // TSReader::SeekTime expect a sample already read to be able to find the correct position of the requested PTS
  if (AP4_FAILED(ReadSample()))
      return false;

  // compensate the pts diff with the manifest timing
  if (isManifestPts)
    pts += m_ptsDiff;

  AP4_UI64 seekPos((pts * 9) / 100);
  if (TSReader::SeekTime(seekPos))
  {
    m_started = true;
    // TSReader::SeekTime may have read other packages to find the requested PTS, so update the PTS/DTS values
    UpdatePts();
    return true;
  }
  return false; // EOS
}
