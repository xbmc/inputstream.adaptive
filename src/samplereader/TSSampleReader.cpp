/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TSSampleReader.h"

#include "AdaptiveByteStream.h"

CTSSampleReader::CTSSampleReader(AP4_ByteStream* input,
                               INPUTSTREAM_TYPE type,
                               AP4_UI32 streamId,
                               uint32_t requiredMask)
  : TSReader{input, requiredMask},
    m_adByteStream{dynamic_cast<CAdaptiveByteStream*>(input)},
    m_typeMask{1U << type}
{
  m_typeMap[INPUTSTREAM_TYPE_NONE] = streamId;
  m_typeMap[type] = streamId;
}

bool CTSSampleReader::Initialize(SESSION::CStream* stream)
{
  return TSReader::Initialize();
}

void CTSSampleReader::AddStreamType(INPUTSTREAM_TYPE type, uint32_t sid)
{
  m_typeMap[type] = sid;
  m_typeMask |= (1 << type);
  if (m_started)
    StartStreaming(m_typeMask);
}

void CTSSampleReader::SetStreamType(INPUTSTREAM_TYPE type, uint32_t sid)
{
  m_typeMap[type] = sid;
  m_typeMask = (1 << type);
}

bool CTSSampleReader::RemoveStreamType(INPUTSTREAM_TYPE type)
{
  m_typeMask &= ~(1 << type);
  StartStreaming(m_typeMask);
  return m_typeMask == 0;
}

AP4_Result CTSSampleReader::Start(bool& bStarted)
{
  bStarted = false;
  if (m_started)
    return AP4_SUCCESS;

  if (!StartStreaming(m_typeMask))
  {
    m_eos = true;
    return AP4_ERROR_CANNOT_OPEN_FILE;
  }

  m_started = true;
  bStarted = true;
  return ReadSample();
}

AP4_Result CTSSampleReader::ReadSample()
{
  if (ReadPacket())
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

bool CTSSampleReader::TimeSeek(uint64_t pts, bool preceeding)
{
  if (!StartStreaming(m_typeMask))
    return false;

  AP4_UI64 seekPos((pts * 9) / 100);
  if (TSReader::SeekTime(seekPos, preceeding))
  {
    m_started = true;
    return AP4_SUCCEEDED(ReadSample());
  }
  return AP4_ERROR_EOS;
}
