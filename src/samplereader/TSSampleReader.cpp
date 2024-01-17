/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TSSampleReader.h"

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

bool CTSSampleReader::Initialize()
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
