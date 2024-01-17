/*
 *  Copyright (C) 2017 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TSReader.h"

#include "mpegts/ES_AAC.h"
#include "mpegts/debug.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <stdlib.h>

#include <bento4/Ap4ByteStream.h>

using namespace UTILS;

namespace
{
void DebugLog(int level, char* msg)
{
  if (msg[std::strlen(msg) - 1] == '\n')
    msg[std::strlen(msg) - 1] = '\0';

  switch (level)
  {
    case DEMUX_DBG_ERROR:
      LOG::Log(LOGERROR, msg);
      break;
    case DEMUX_DBG_WARN:
      LOG::Log(LOGWARNING, msg);
      break;
    case DEMUX_DBG_INFO:
      LOG::Log(LOGINFO, msg);
      break;
    case DEMUX_DBG_DEBUG:
      [[fallthrough]];
    case DEMUX_DBG_PARSE:
      LOG::Log(LOGDEBUG, msg);
      break;
    default:
      break;
  }
}
} // unnamed namespace

TSReader::TSReader(AP4_ByteStream* stream, uint32_t requiredMask)
  : m_stream(stream), m_requiredMask(requiredMask), m_typeMask(0)
{
  // Uncomment to debug TSDemux library
  // TSDemux::DBGAll();
  // TSDemux::SetDBGMsgCallback(DebugLog);
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
  return AP4_SUCCEEDED(m_stream->Read(data, static_cast<AP4_Size>(len)));
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

bool TSReader::GetInformation(kodi::addon::InputstreamInfo& info)
{
  // Keep it in sync with TSDemux::STREAM_TYPE
  static const char* STREAMTYPEMAP[19] = {
      CODEC::NAME_UNKNOWN, CODEC::NAME_MPEG1, CODEC::NAME_MPEG2,  CODEC::NAME_MPEG1,
      CODEC::NAME_MPEG2,   CODEC::NAME_AAC,   CODEC::NAME_AAC,    CODEC::NAME_AAC,
      CODEC::NAME_H264,    CODEC::NAME_HEVC,  CODEC::NAME_AC3,    CODEC::NAME_EAC3,
      CODEC::NAME_UNKNOWN, CODEC::NAME_SRT,   CODEC::NAME_MPEG4,  CODEC::NAME_VC1,
      CODEC::NAME_UNKNOWN, CODEC::NAME_DTS,   CODEC::NAME_UNKNOWN};

  bool isChanged{false};

  for (auto& tsInfo : m_streamInfos)
  {
    if (tsInfo.m_streamType != info.GetStreamType())
      continue;

    if (!tsInfo.m_changed)
      return false;
    tsInfo.m_changed = false;

    if (tsInfo.m_streamType == INPUTSTREAM_TYPE_VIDEO)
    {
      if (tsInfo.m_stream->stream_info.fps_scale != static_cast<int>(info.GetFpsScale()) ||
          tsInfo.m_stream->stream_info.fps_rate != static_cast<int>(info.GetFpsRate()) ||
          tsInfo.m_stream->stream_info.height != static_cast<int>(info.GetHeight()) ||
          tsInfo.m_stream->stream_info.width != static_cast<int>(info.GetWidth()) ||
          (tsInfo.m_stream->stream_info.aspect > 0 &&
           tsInfo.m_stream->stream_info.aspect != info.GetAspect()))
      {
        info.SetFpsRate(tsInfo.m_stream->stream_info.fps_rate);
        info.SetFpsScale(tsInfo.m_stream->stream_info.fps_scale);
        info.SetWidth(tsInfo.m_stream->stream_info.width);
        info.SetHeight(tsInfo.m_stream->stream_info.height);
        if (tsInfo.m_stream->stream_info.aspect > 0)
          info.SetAspect(tsInfo.m_stream->stream_info.aspect);

        isChanged = true;
      }
    }
    else if (tsInfo.m_streamType == INPUTSTREAM_TYPE_AUDIO)
    {
      if (tsInfo.m_stream->stream_info.language[0])
        info.SetLanguage(tsInfo.m_stream->stream_info.language);

      if (tsInfo.m_stream->stream_info.channels != static_cast<int>(info.GetChannels()) ||
          tsInfo.m_stream->stream_info.sample_rate != static_cast<int>(info.GetSampleRate()) ||
          tsInfo.m_stream->stream_info.block_align != static_cast<int>(info.GetBlockAlign()) ||
          tsInfo.m_stream->stream_info.bit_rate != static_cast<int>(info.GetBitRate()) ||
          tsInfo.m_stream->stream_info.bits_per_sample != static_cast<int>(info.GetBitsPerSample()))
      {
        info.SetChannels(tsInfo.m_stream->stream_info.channels);
        info.SetSampleRate(tsInfo.m_stream->stream_info.sample_rate);
        info.SetBlockAlign(tsInfo.m_stream->stream_info.block_align);
        info.SetBitRate(tsInfo.m_stream->stream_info.bit_rate);
        info.SetBitsPerSample(tsInfo.m_stream->stream_info.bits_per_sample);
        isChanged = true;
      }
    }

    const char* codecName = STREAMTYPEMAP[tsInfo.m_stream->stream_type];
    if (info.GetCodecName() != codecName)
    {
      info.SetCodecName(codecName);
      isChanged = true;
    }

    STREAMCODEC_PROFILE codecProfile{CodecProfileUnknown};
    if (codecName == CODEC::NAME_AAC)
    {
      int tsCodecProfile = tsInfo.m_stream->stream_info.codecProfile;
      if (tsCodecProfile == TSDemux::ES_AAC::PROFILE_MAIN)
        codecProfile = AACCodecProfileMAIN;
      else if (tsCodecProfile == TSDemux::ES_AAC::PROFILE_LC)
        codecProfile = AACCodecProfileLOW;
      else if (tsCodecProfile == TSDemux::ES_AAC::PROFILE_SSR)
        codecProfile = AACCodecProfileSSR;
      else if (tsCodecProfile == TSDemux::ES_AAC::PROFILE_LTP)
        codecProfile = AACCodecProfileLTP;
    }

    if (codecProfile != CodecProfileUnknown && info.GetCodecProfile() != codecProfile)
    {
      info.SetCodecProfile(codecProfile);
      isChanged = true;
    }

    if (!info.CompareExtraData(tsInfo.m_stream->stream_info.extra_data,
                               tsInfo.m_stream->stream_info.extra_data_size))
    {
      info.SetExtraData(tsInfo.m_stream->stream_info.extra_data,
                        tsInfo.m_stream->stream_info.extra_data_size);
      isChanged = true;
    }
    break;
  }

  return isChanged;
}

// We assume that m_startpos is the current I-Frame position
bool TSReader::SeekTime(uint64_t timeInTs, bool preceeding)
{
  bool hasVideo(false);
  //look if we have video
  for (auto &tsInfo : m_streamInfos)
    if (tsInfo.m_enabled && tsInfo.m_streamType == INPUTSTREAM_TYPE_VIDEO)
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
      tsInfo.m_streamType = INPUTSTREAM_TYPE_VIDEO;
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
      tsInfo.m_streamType = INPUTSTREAM_TYPE_AUDIO;
      break;
    case TSDemux::STREAM_TYPE_DVB_SUBTITLE:
      tsInfo.m_streamType = INPUTSTREAM_TYPE_SUBTITLE;
      break;
    default:
      tsInfo.m_streamType = INPUTSTREAM_TYPE_NONE;
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

const INPUTSTREAM_TYPE TSReader::GetStreamType() const
{
  for (const auto &tsInfo : m_streamInfos)
    if (tsInfo.m_stream && tsInfo.m_stream->pid == m_pkt.pid)
      return tsInfo.m_streamType;
  return INPUTSTREAM_TYPE_NONE;
}
