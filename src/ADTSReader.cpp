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

#include <stdlib.h>

#include <bento4/Ap4ByteStream.h>
using namespace adaptive;

uint64_t ID3TAG::getSize(const uint8_t *data, unsigned int len, unsigned int shift)
{
  uint64_t size(0);
  const uint8_t *dataE(data + len);
  for (; data < dataE; ++data)
    size = size << shift | *data;
  return size;
};

ID3TAG::PARSECODE ID3TAG::parse(AP4_ByteStream *stream)
{
  uint8_t buffer[64];
  unsigned int retCount(0);

  while (!AP4_SUCCEEDED(stream->Read(buffer, HEADER_SIZE)))
  {
    if (retCount++)
      return PARSE_FAIL;
  }

  if (memcmp(buffer, "ID3", 3) != 0)
  {
    AP4_Position pos;
    stream->Tell(pos);
    stream->Seek(pos - HEADER_SIZE);
    return PARSE_NO_ID3;
  }

  m_majorVer = buffer[3];
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

bool ADTSFrame::parse(AP4_ByteStream* stream)
{
  AdtsType adtsType = CAdaptiveAdtsHeaderParser::GetAdtsType(stream);
  switch (adtsType)
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
  AP4_DataBuffer buffer;
  buffer.SetDataSize(16);

  if (!AP4_SUCCEEDED(stream->Read(buffer.UseData(), AP4_ADTS_HEADER_SIZE)))
    return false;

  CAdaptiveAdtsParser parser;
  AP4_AacFrame frame;
  AP4_Size sz = buffer.GetDataSize();
  parser.Feed(buffer.GetData(), &sz);
  AP4_Result result = parser.FindFrameHeader(frame);
  if (!AP4_SUCCEEDED(result))
    return false;

  m_totalSize = frame.m_Info.m_FrameLength + AP4_ADTS_HEADER_SIZE;
  m_frameCount = 1024;
  m_summedFrameCount += m_frameCount;
  m_sampleRate = frame.m_Info.m_SamplingFrequency;
  m_channelCount = frame.m_Info.m_ChannelConfiguration;

  // rewind stream to beginning of syncframe
  AP4_Position currentPos;
  stream->Tell(currentPos);
  stream->Seek(currentPos - (AP4_ADTS_HEADER_SIZE));

  m_dataBuffer.SetDataSize(m_totalSize);
  if (!AP4_SUCCEEDED(stream->Read(m_dataBuffer.UseData(), m_dataBuffer.GetDataSize())))
    return false;

  AdjustStreamForPadding(stream);
  return true;
}

bool ADTSFrame::ParseAc3(AP4_ByteStream* stream)
{
  AP4_DataBuffer buffer;
  buffer.SetDataSize(AP4_AC3_HEADER_SIZE);

  if (!AP4_SUCCEEDED(stream->Read(buffer.UseData(), AP4_AC3_HEADER_SIZE)))
    return false;

  CAdaptiveAc3Parser parser;
  AP4_Ac3Frame frame;
  AP4_Size sz = buffer.GetDataSize();
  parser.Feed(buffer.GetData(), &sz);
  AP4_Result result = parser.FindFrameHeader(frame);
  if (!AP4_SUCCEEDED(result))
    return false;

  m_totalSize = frame.m_Info.m_FrameSize;
  m_sampleRate = frame.m_Info.m_SampleRate;
  m_channelCount = frame.m_Info.m_ChannelCount;
  m_frameCount = 256 * m_channelCount;
  m_summedFrameCount += m_frameCount;

  // rewind stream to beginning of syncframe
  AP4_Position currentPos;
  stream->Tell(currentPos);
  stream->Seek(currentPos - (AP4_AC3_HEADER_SIZE));

  m_dataBuffer.SetDataSize(m_totalSize);
  if (!AP4_SUCCEEDED(stream->Read(m_dataBuffer.UseData(), m_dataBuffer.GetDataSize())))
    return false;

  AdjustStreamForPadding(stream);
  return true;
}

bool ADTSFrame::ParseEc3(AP4_ByteStream* stream)
{
  AP4_DataBuffer buffer;
  buffer.SetDataSize(AP4_EAC3_HEADER_SIZE);

  if (!AP4_SUCCEEDED(stream->Read(buffer.UseData(), AP4_EAC3_HEADER_SIZE)))
    return false;

  CAdaptiveEac3Parser parser;
  AP4_Eac3Frame frame;
  AP4_Size sz = buffer.GetDataSize();
  parser.Feed(buffer.GetData(), &sz);
  AP4_Result result = parser.FindFrameHeader(frame);
  if (!AP4_SUCCEEDED(result))
    return false;

  m_totalSize = frame.m_Info.m_FrameSize;
  m_sampleRate = frame.m_Info.m_SampleRate;
  m_channelCount = frame.m_Info.m_ChannelCount;
  m_frameCount = 256 * m_channelCount;
  m_summedFrameCount += m_frameCount;

  // rewind stream to beginning of syncframe
  AP4_Position currentPos;
  stream->Tell(currentPos);
  stream->Seek(currentPos - (AP4_EAC3_HEADER_SIZE));

  m_dataBuffer.SetDataSize(m_totalSize);
  if (!AP4_SUCCEEDED(stream->Read(m_dataBuffer.UseData(), m_dataBuffer.GetDataSize())))
    return false;

  AdjustStreamForPadding(stream);
  return true;
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
  return false;
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
      break;

    if (m_id3TagParser.getPts(m_basePts))
      m_frameParser.resetFrameCount();

    m_pts = m_basePts + m_frameParser.getPtsOffset();

    return m_frameParser.parse(m_stream);
  }
  return true;
}
