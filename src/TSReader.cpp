/*
*      Copyright (C) 2017 peak3d
*      http://www.peak3d.de
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
*  <http://www.gnu.org/licenses/>.
*
*/

#include "TSReader.h"
#include "Ap4ByteStream.h"
#include <stdlib.h>

TSReader::TSReader(AP4_ByteStream *stream, uint32_t requiredMask)
  : m_stream(stream)
  , m_requiredMask(requiredMask)
  , m_typeMask(0)
{
}

bool TSReader::Initialize()
{
  m_AVContext = new TSDemux::AVContext(this, 0, 0);
  // Get stream Information
  if (!ReadPacket(true))
  {
    delete m_AVContext;
    m_AVContext = nullptr;
    return false;
  }
  return true;
}

TSReader::~TSReader()
{
  delete m_AVContext;
  m_AVContext = nullptr;
}

bool TSReader::ReadAV(uint64_t pos, unsigned char * data, size_t len)
{
  m_stream->Seek(pos);
  return AP4_SUCCEEDED(m_stream->Read(data, len));
}

void TSReader::Reset(bool resetPackets)
{
  m_stream->Tell(m_startPos);
  m_AVContext->GoPosition(m_startPos, resetPackets);
  //mark invalid for Seek operations
  m_pkt.pts = PTS_UNSET;
}

bool TSReader::StartStreaming(AP4_UI32 typeMask)
{
  m_typeMask = typeMask;
  // All streams are ON at this place
  for (auto &tsInfo : m_streamInfos)
  {
    if (!(typeMask & (1 << tsInfo.m_streamType)))
      m_AVContext->StopStreaming(tsInfo.m_stream->pid);
    else
      m_AVContext->StartStreaming(tsInfo.m_stream->pid);
    tsInfo.m_enabled = (typeMask & (1 << tsInfo.m_streamType)) != 0;
    typeMask &= ~(1 << tsInfo.m_streamType);
  }
  return typeMask == 0;
}

bool TSReader::GetInformation(INPUTSTREAM_INFO &info)
{
  static const char* STREAMTYPEMAP[] = {
    "unk", "mpeg1", "mpeg2", "mpeg1", "mpeg2", "aac", "aac", "aac", "h264", "hevc", "ac3", "eac3", "unk", "srt", "mpeg4", "vc1", "unk", "unk", "unk"
  };

  for (auto &tsInfo : m_streamInfos)
  {
    if (tsInfo.m_streamType == info.m_streamType)
    {
      if (!tsInfo.m_changed)
        return false;
      tsInfo.m_changed = false;

      bool ret(false);

      if (tsInfo.m_streamType == INPUTSTREAM_INFO::TYPE_VIDEO)
      {
        if ((!info.m_FpsScale && tsInfo.m_stream->stream_info.fps_scale != static_cast<int>(info.m_FpsScale)) ||
          (!info.m_FpsRate && tsInfo.m_stream->stream_info.fps_rate != static_cast<int>(info.m_FpsRate)) ||
          (tsInfo.m_stream->stream_info.height != static_cast<int>(info.m_Height)) ||
          (tsInfo.m_stream->stream_info.width != static_cast<int>(info.m_Width)) ||
          (tsInfo.m_stream->stream_info.aspect && tsInfo.m_stream->stream_info.aspect != info.m_Aspect))
        {
          info.m_FpsRate = tsInfo.m_stream->stream_info.fps_rate;
          info.m_FpsScale = tsInfo.m_stream->stream_info.fps_scale;
          info.m_Width = tsInfo.m_stream->stream_info.width;
          info.m_Height = tsInfo.m_stream->stream_info.height;
          if (tsInfo.m_stream->stream_info.aspect)
            info.m_Aspect = tsInfo.m_stream->stream_info.aspect;
          ret = true;
        }
      }
      else if (tsInfo.m_streamType == INPUTSTREAM_INFO::TYPE_AUDIO)
      {
        if (tsInfo.m_stream->stream_info.language[0])
          strncpy(info.m_language, tsInfo.m_stream->stream_info.language, 3), info.m_language[3] = 0;

        if ((tsInfo.m_stream->stream_info.channels != static_cast<int>(info.m_Channels)) ||
          (tsInfo.m_stream->stream_info.sample_rate != static_cast<int>(info.m_SampleRate)) ||
          (tsInfo.m_stream->stream_info.block_align != static_cast<int>(info.m_BlockAlign)) ||
          (tsInfo.m_stream->stream_info.bit_rate != static_cast<int>(info.m_BitRate)) ||
          (tsInfo.m_stream->stream_info.bits_per_sample != static_cast<int>(info.m_BitsPerSample)))
        {
          info.m_Channels = tsInfo.m_stream->stream_info.channels;
          info.m_SampleRate = tsInfo.m_stream->stream_info.sample_rate;
          info.m_BlockAlign = tsInfo.m_stream->stream_info.block_align;
          info.m_BitRate = tsInfo.m_stream->stream_info.bit_rate;
          info.m_BitsPerSample = tsInfo.m_stream->stream_info.bits_per_sample;
          ret = true;
        }
      }
      strcpy(info.m_codecName, STREAMTYPEMAP[tsInfo.m_stream->stream_type]);

      if (info.m_ExtraSize != tsInfo.m_stream->stream_info.extra_data_size
        || memcmp(info.m_ExtraData, tsInfo.m_stream->stream_info.extra_data, info.m_ExtraSize))
      {
        info.m_ExtraData = (uint8_t*)realloc((void*)info.m_ExtraData, tsInfo.m_stream->stream_info.extra_data_size);
        memcpy((void*)info.m_ExtraData, tsInfo.m_stream->stream_info.extra_data, tsInfo.m_stream->stream_info.extra_data_size);
        info.m_ExtraSize = tsInfo.m_stream->stream_info.extra_data_size;
      }
      return ret;
    }
  }
  return false;
}

// We assume that m_startpos is the current I-Frame position
bool TSReader::SeekTime(uint64_t timeInTs, bool preceeding)
{
  bool hasVideo(false);
  //look if we have video
  for (auto &tsInfo : m_streamInfos)
    if (tsInfo.m_enabled && tsInfo.m_streamType == INPUTSTREAM_INFO::TYPE_VIDEO)
    {
      hasVideo = true;
      break;
    }

  uint64_t lastRecovery(static_cast<uint64_t>(m_startPos));
  while (m_pkt.pts == PTS_UNSET || !preceeding || static_cast<uint64_t>(m_pkt.pts) < timeInTs)
  {
    uint64_t thisFrameStart(m_AVContext->GetRecoveryPos());
    if (!ReadPacket())
      return false;
    if (!hasVideo || m_pkt.recoveryPoint || thisFrameStart == m_startPos)
    {
      lastRecovery = thisFrameStart;
      if (!preceeding && static_cast<uint64_t>(m_pkt.pts) >= timeInTs)
        break;
    }
  }
  m_AVContext->GoPosition(lastRecovery, true);

  return true;
}

bool TSReader::ReadPacket(bool scanStreamInfo)
{
  if (!m_AVContext)
    return false;

  bool ret(false);

  if (GetPacket())
    return true;

  while (!ret)
  {
    int errorCode;
    if ((errorCode = m_AVContext->TSResync()) != TSDemux::AVCONTEXT_CONTINUE)
    {
      if (errorCode != TSDemux::AVCONTEXT_IO_ERROR)
        return false;
      //Lets make one retry with the new segment
      Reset(false);
      if (m_AVContext->TSResync() != TSDemux::AVCONTEXT_CONTINUE)
        return false;
    }

    int status = m_AVContext->ProcessTSPacket();

    while (GetPacket())
    {
      if (scanStreamInfo)
      {
        if (m_pkt.streamChange)
        {
          if (HandleStreamChange(m_pkt.pid))
          {
            m_AVContext->GoPosition(m_startPos, true);
            StartStreaming(m_typeMask);
            return true;
          }
        }
      }
      else
      {
        if (m_pkt.streamChange)
          HandleStreamChange(m_pkt.pid);
        return true;
      }
    }

    if (m_AVContext->HasPIDPayload())
    {
      status = m_AVContext->ProcessTSPayload();
      if (status == TSDemux::AVCONTEXT_PROGRAM_CHANGE)
      {
        if (HandleProgramChange())
        {
          if (scanStreamInfo)
            ret = true;
        }
        else
        {
          scanStreamInfo = true;
          m_startPos = m_AVContext->GetNextPosition();
        }
      }
    }

    if (status == TSDemux::AVCONTEXT_TS_ERROR)
      m_AVContext->Shift();
    else
      m_AVContext->GoNext();
  }
  return true;
}

bool TSReader::GetPacket()
{
  if (m_AVContext->HasPIDStreamData())
  {
    TSDemux::ElementaryStream* es = m_AVContext->GetPIDStream();
    if (!es)
      return false;

    return es->GetStreamPacket(&m_pkt);
  }
  return false;
}

bool TSReader::HandleProgramChange()
{
  bool ret = true;
  m_streamInfos.clear();

  std::vector<TSDemux::ElementaryStream*> streams = m_AVContext->GetStreams();
  for (auto stream : streams)
  {
    m_streamInfos.push_back(TSINFO(stream));
    TSINFO &tsInfo(m_streamInfos.back());

    switch (tsInfo.m_stream->stream_type)
    {
    case TSDemux::STREAM_TYPE_VIDEO_MPEG1:
    case TSDemux::STREAM_TYPE_VIDEO_MPEG2:
    case TSDemux::STREAM_TYPE_VIDEO_H264:
    case TSDemux::STREAM_TYPE_VIDEO_HEVC:
    case TSDemux::STREAM_TYPE_VIDEO_MPEG4:
    case TSDemux::STREAM_TYPE_VIDEO_VC1:
      tsInfo.m_streamType = INPUTSTREAM_INFO::TYPE_VIDEO;
      break;
    case TSDemux::STREAM_TYPE_AUDIO_MPEG1:
    case TSDemux::STREAM_TYPE_AUDIO_MPEG2:
    case TSDemux::STREAM_TYPE_AUDIO_AAC:
    case TSDemux::STREAM_TYPE_AUDIO_AAC_ADTS:
    case TSDemux::STREAM_TYPE_AUDIO_AAC_LATM:
    case TSDemux::STREAM_TYPE_AUDIO_AC3:
    case TSDemux::STREAM_TYPE_AUDIO_EAC3:
    case TSDemux::STREAM_TYPE_AUDIO_LPCM:
    case TSDemux::STREAM_TYPE_AUDIO_DTS:
      tsInfo.m_streamType = INPUTSTREAM_INFO::TYPE_AUDIO;
      break;
    case TSDemux::STREAM_TYPE_DVB_SUBTITLE:
      tsInfo.m_streamType = INPUTSTREAM_INFO::TYPE_SUBTITLE;
      break;
    default:
      tsInfo.m_streamType = INPUTSTREAM_INFO::TYPE_NONE;
    }

    if (stream->has_stream_info)
      HandleStreamChange(stream->pid);
    else if (m_requiredMask & (1 << tsInfo.m_streamType))
      ret = false;
    else
    {
      tsInfo.m_needInfo = false;
      continue;
    }
    m_AVContext->StartStreaming(stream->pid);
  }
  return ret;
}

bool TSReader::HandleStreamChange(uint16_t pid)
{
  bool ret(true);
  for (auto &tsInfo : m_streamInfos)
  {
    if (tsInfo.m_stream->pid == pid)
    {
      tsInfo.m_needInfo = false;
      tsInfo.m_changed = true;
    }
    else if (tsInfo.m_needInfo)
      ret = false;
  }
  return ret;
}

const INPUTSTREAM_INFO::STREAM_TYPE TSReader::GetStreamType() const
{
  for (const auto &tsInfo : m_streamInfos)
    if (tsInfo.m_stream && tsInfo.m_stream->pid == m_pkt.pid)
      return tsInfo.m_streamType;
  return INPUTSTREAM_INFO::TYPE_NONE;
}
