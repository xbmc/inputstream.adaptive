/*
 *  Copyright (C) 2017 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ADTSReader.h"

#include "parser/CodecParser.h"
#include "utils/log.h"
#include "utils/Utils.h"

#include <stdlib.h>

#include <bento4/Ap4ByteStream.h>

using namespace adaptive;
using namespace UTILS;

uint64_t ID3TAG::getSize(const uint8_t* data, unsigned int len, unsigned int shift)
{
  uint64_t size(0);
  const uint8_t* dataE(data + len);
  for (; data < dataE; ++data)
    size = size << shift | *data;
  return size;
};

ID3TAG::PARSECODE ID3TAG::parse(AP4_ByteStream* stream)
{
  uint8_t buffer[64];
  unsigned int retCount(0);

  while (!AP4_SUCCEEDED(stream->Read(buffer, HEADER_SIZE)))
  {
    if (retCount++)
      return PARSE_FAIL;
  }

  // ID3v2 header
  // 3 byte "ID3" + 1 byte ver + 1 byte revision + 1 byte flags + 4 byte size
  if (memcmp(buffer, "ID3", 3) != 0)
  {
    AP4_Position pos;
    stream->Tell(pos);
    stream->Seek(pos - HEADER_SIZE);
    return PARSE_NO_ID3;
  }

  m_majorVer = buffer[3];
  m_revisionVer = buffer[4];
  m_flags = buffer[5];
  uint32_t size = static_cast<uint32_t>(getSize(buffer + 6, 4, 7));

  //iterate through frames and search timestamp
  while (size > HEADER_SIZE)
  {
    if (!AP4_SUCCEEDED(stream->Read(buffer, HEADER_SIZE)))
      return PARSE_FAIL;

    uint32_t frameSize = static_cast<uint32_t>(getSize(buffer + 4, 4, 8));

    if (memcmp(buffer, "PRIV", 4) == 0 && frameSize == 53)
    {
      if (!AP4_SUCCEEDED(stream->Read(buffer, frameSize)))
        return PARSE_FAIL;

      // HLS audio packet timestamp: https://datatracker.ietf.org/doc/html/rfc8216
      if (strncmp(reinterpret_cast<const char*>(buffer), "com.apple.streaming.transportStreamTimestamp", 44) == 0 && buffer[44] == 0)
      {
        m_timestamp = getSize(buffer + 45, 8, 8);
      }
    }
    else
    {
      AP4_Position curPos;
      stream->Tell(curPos);
      stream->Seek(curPos + frameSize);
    }
    size -= (HEADER_SIZE + frameSize);
  }
  return PARSE_SUCCESS;
}

void ID3TAG::SkipID3Data(AP4_ByteStream* stream)
{
  uint8_t buffer[64];

  if (!AP4_SUCCEEDED(stream->Read(buffer, HEADER_SIZE)))
  {
    // Try again
    if (!AP4_SUCCEEDED(stream->Read(buffer, HEADER_SIZE)))
      return;
  }

  if (std::memcmp(buffer, "ID3", 3) != 0) // No ID3 header
  {
    AP4_Position currentPos;
    stream->Tell(currentPos);
    stream->Seek(currentPos - HEADER_SIZE);
    return;
  }

  // Get ID3v2 data size
  uint32_t headerSize = static_cast<uint32_t>(getSize(buffer + 6, 4, 7));

  AP4_Position currentPos;
  stream->Tell(currentPos);
  stream->Seek(currentPos + headerSize);
}

/**********************************************************************************************************************************/

void ADTSFrame::AdjustStreamForPadding(AP4_ByteStream* stream)
{
  AP4_Position currentPos;
  AP4_Position newPos;
  stream->Tell(currentPos);
  stream->Seek(currentPos + 16);
  stream->Tell(newPos);
  if (newPos - currentPos == 16)
    stream->Seek(currentPos);
}

ADTSFrame::ADTSFrameInfo ADTSFrame::GetFrameInfo(AP4_ByteStream* stream)
{
  ADTSFrameInfo frameInfo;
  frameInfo.m_codecType = CAdaptiveAdtsHeaderParser::GetAdtsType(stream);

  switch (frameInfo.m_codecType)
  {
    case AdtsType::AAC:
      ParseAacHeader(stream, frameInfo);
      break;
    case AdtsType::AC3:
      ParseAc3Header(stream, frameInfo);
      break;
    case AdtsType::EAC3:
      ParseEc3Header(stream, frameInfo);
      break;
    case AdtsType::AC4:
      break;
    default:
      break;
  }
  return frameInfo;
}

bool ADTSFrame::parse(AP4_ByteStream* stream)
{
  m_frameInfo.m_codecType = CAdaptiveAdtsHeaderParser::GetAdtsType(stream);
  switch (m_frameInfo.m_codecType)
  {
    case AdtsType::AAC:
      return ParseAac(stream);
    case AdtsType::AC3:
      return ParseAc3(stream);
    case AdtsType::EAC3:
      return ParseEc3(stream);
    case AdtsType::AC4:
      return false;
    default:
      return false;
  }
}

bool ADTSFrame::ParseAac(AP4_ByteStream* stream)
{
  if (!ParseAacHeader(stream, m_frameInfo))
    return false;

  m_summedFrameCount += m_frameInfo.m_frameCount;

  // rewind stream to beginning of syncframe
  AP4_Position currentPos;
  stream->Tell(currentPos);
  stream->Seek(currentPos - (AP4_ADTS_HEADER_SIZE));

  m_dataBuffer.SetDataSize(m_frameInfo.m_frameSize);
  if (!AP4_SUCCEEDED(stream->Read(m_dataBuffer.UseData(), m_dataBuffer.GetDataSize())))
    return false;

  AdjustStreamForPadding(stream);
  return true;
}

bool ADTSFrame::ParseAacHeader(AP4_ByteStream* stream, ADTSFrameInfo& frameInfo)
{
  AP4_DataBuffer buffer;
  buffer.SetDataSize(16);

  if (!AP4_SUCCEEDED(stream->Read(buffer.UseData(), AP4_ADTS_HEADER_SIZE)))
    return false;

  CAdaptiveAdtsParser parser;
  AP4_Size sz = buffer.GetDataSize();
  parser.Feed(buffer.GetData(), &sz);

  AP4_AacFrame frame;
  AP4_Result result = parser.FindFrameHeader(frame);
  if (!AP4_SUCCEEDED(result))
    return false;

  frameInfo.m_codecProfile = frame.m_Info.m_Profile;
  frameInfo.m_frameSize = frame.m_Info.m_FrameLength + AP4_ADTS_HEADER_SIZE;
  frameInfo.m_frameCount = 1024;
  frameInfo.m_sampleRate = frame.m_Info.m_SamplingFrequency;
  frameInfo.m_channels = frame.m_Info.m_ChannelConfiguration;
  return true;
}

bool ADTSFrame::ParseAc3(AP4_ByteStream* stream)
{
  if (!ParseAc3Header(stream, m_frameInfo))
    return false;

  m_summedFrameCount += m_frameInfo.m_frameCount;

  // rewind stream to beginning of syncframe
  AP4_Position currentPos;
  stream->Tell(currentPos);
  stream->Seek(currentPos - (AP4_AC3_HEADER_SIZE));

  m_dataBuffer.SetDataSize(m_frameInfo.m_frameSize);
  if (!AP4_SUCCEEDED(stream->Read(m_dataBuffer.UseData(), m_dataBuffer.GetDataSize())))
    return false;

  AdjustStreamForPadding(stream);
  return true;
}

bool ADTSFrame::ParseAc3Header(AP4_ByteStream* stream, ADTSFrameInfo& frameInfo)
{
  AP4_DataBuffer buffer;
  buffer.SetDataSize(AP4_AC3_HEADER_SIZE);

  if (!AP4_SUCCEEDED(stream->Read(buffer.UseData(), AP4_AC3_HEADER_SIZE)))
    return false;

  CAdaptiveAc3Parser parser;
  AP4_Size sz = buffer.GetDataSize();
  parser.Feed(buffer.GetData(), &sz);

  AP4_Ac3Frame frame;
  AP4_Result result = parser.FindFrameHeader(frame);
  if (!AP4_SUCCEEDED(result))
    return false;

  frameInfo.m_frameSize = frame.m_Info.m_FrameSize;
  frameInfo.m_frameCount = 256u * frame.m_Info.m_ChannelCount;
  frameInfo.m_sampleRate = frame.m_Info.m_SampleRate;
  frameInfo.m_channels = frame.m_Info.m_ChannelCount;
  return true;
}

bool ADTSFrame::ParseEc3(AP4_ByteStream* stream)
{
  if (!ParseEc3Header(stream, m_frameInfo))
    return false;

  m_summedFrameCount += m_frameInfo.m_frameCount;

  // rewind stream to beginning of syncframe
  AP4_Position currentPos;
  stream->Tell(currentPos);
  stream->Seek(currentPos - (AP4_EAC3_HEADER_SIZE));

  m_dataBuffer.SetDataSize(m_frameInfo.m_frameSize);
  if (!AP4_SUCCEEDED(stream->Read(m_dataBuffer.UseData(), m_dataBuffer.GetDataSize())))
    return false;

  AdjustStreamForPadding(stream);
  return true;
}

bool ADTSFrame::ParseEc3Header(AP4_ByteStream* stream, ADTSFrameInfo& frameInfo)
{
  AP4_DataBuffer buffer;
  buffer.SetDataSize(AP4_EAC3_HEADER_SIZE);

  if (!AP4_SUCCEEDED(stream->Read(buffer.UseData(), AP4_EAC3_HEADER_SIZE)))
    return false;

  CAdaptiveEac3Parser parser;
  AP4_Size sz = buffer.GetDataSize();
  parser.Feed(buffer.GetData(), &sz);

  AP4_Eac3Frame frame;
  AP4_Result result = parser.FindFrameHeader(frame);
  if (!AP4_SUCCEEDED(result))
    return false;

  frameInfo.m_frameSize = frame.m_Info.m_FrameSize;
  frameInfo.m_frameCount = 256u * frame.m_Info.m_ChannelCount;
  frameInfo.m_sampleRate = frame.m_Info.m_SampleRate;
  if (frame.m_Info.complexity_index_type_a > 0)
  {
    // The channels value should match the value of the complexity_index_type_a field
    frameInfo.m_channels = frame.m_Info.complexity_index_type_a;
    frameInfo.m_codecFlags |= CODEC_FLAG_ATMOS;
  }
  else
  {
    frameInfo.m_channels = frame.m_Info.m_ChannelCount;
    frameInfo.m_codecFlags &= ~CODEC_FLAG_ATMOS;
  }
  return true;
}

void ADTSFrame::reset()
{
  m_summedFrameCount = 0;
  m_frameInfo.m_frameCount = 0;
  m_dataBuffer.SetDataSize(0);
}

uint64_t ADTSFrame::getPtsOffset() const
{
  if (m_frameInfo.m_sampleRate > 0)
    return (m_summedFrameCount * 90000) / m_frameInfo.m_sampleRate;

  return 0;
}

uint64_t ADTSFrame::getDuration() const
{
  if (m_frameInfo.m_sampleRate > 0)
    return (static_cast<uint64_t>(m_frameInfo.m_frameCount) * 90000) / m_frameInfo.m_sampleRate;

  return 0;
}

/**********************************************************************************************************************************/


ADTSReader::ADTSReader(AP4_ByteStream *stream)
  : m_stream(stream)
{
}

ADTSReader::~ADTSReader()
{
}

void ADTSReader::Reset()
{
  m_pts = ADTS_PTS_UNSET;
  m_frameParser.reset();
}

bool ADTSReader::GetInformation(kodi::addon::InputstreamInfo& info)
{
  m_id3TagParser.SkipID3Data(m_stream);
  ADTSFrame::ADTSFrameInfo frameInfo = m_frameParser.GetFrameInfo(m_stream);

  m_stream->Seek(0); // Seek back because data has been consumed

  if (frameInfo.m_codecType == AdtsType::NONE)
    return false;

  std::string codecName;
  STREAMCODEC_PROFILE codecProfile{CodecProfileUnknown};

  if (frameInfo.m_codecType == AdtsType::AAC)
  {
    codecName = CODEC::NAME_AAC;
    if (frameInfo.m_codecProfile == AP4_AAC_PROFILE_MAIN)
      codecProfile = AACCodecProfileMAIN;
    else if (frameInfo.m_codecProfile == AP4_AAC_PROFILE_LC)
      codecProfile = AACCodecProfileLOW;
    else if (frameInfo.m_codecProfile == AP4_AAC_PROFILE_SSR)
      codecProfile = AACCodecProfileSSR;
    else if (frameInfo.m_codecProfile == AP4_AAC_PROFILE_LTP)
      codecProfile = AACCodecProfileLTP;
  }
  else if (frameInfo.m_codecType == AdtsType::AC3)
  {
    codecName = CODEC::NAME_AC3;
  }
  else if (frameInfo.m_codecType == AdtsType::EAC3)
  {
    codecName = CODEC::NAME_EAC3;
    if ((frameInfo.m_codecFlags & ADTSFrame::CODEC_FLAG_ATMOS) == ADTSFrame::CODEC_FLAG_ATMOS)
      codecProfile = DDPlusCodecProfileAtmos;
  }

  bool isChanged{false};

  if (!codecName.empty() && info.GetCodecName() != codecName)
  {
    info.SetCodecName(codecName);
    isChanged = true;
  }
  if (codecProfile != CodecProfileUnknown && info.GetCodecProfile() != codecProfile)
  {
    info.SetCodecProfile(codecProfile);
    isChanged = true;
  }
  if (info.GetChannels() != frameInfo.m_channels)
  {
    info.SetChannels(frameInfo.m_channels);
    isChanged = true;
  }
  if (info.GetSampleRate() != frameInfo.m_sampleRate)
  {
    info.SetSampleRate(frameInfo.m_sampleRate);
    isChanged = true;
  }

  return isChanged;
}

// We assume that m_startpos is the current I-Frame position
bool ADTSReader::SeekTime(uint64_t timeInTs, bool preceeding)
{
  while (m_pts < timeInTs)
    if (!ReadPacket())
      return false;
  return true;
}

bool ADTSReader::ReadPacket()
{
  ID3TAG::PARSECODE id3Ret;
  while (true)
  {
    if ((id3Ret = m_id3TagParser.parse(m_stream)) == ID3TAG::PARSE_SUCCESS)
      continue;
    else if (id3Ret == ID3TAG::PARSE_FAIL)
      return false;

    if (m_id3TagParser.getPts(m_basePts))
      m_frameParser.resetFrameCount();

    m_pts = m_basePts + m_frameParser.getPtsOffset();

    return m_frameParser.parse(m_stream);
  }
  return true;
}
