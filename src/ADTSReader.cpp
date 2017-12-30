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

#include "ADTSReader.h"
#include "Ap4ByteStream.h"
#include <stdlib.h>

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

uint64_t ADTSFrame::getBE(const uint8_t *data, unsigned int len)
{
  uint64_t size(0);
  const uint8_t *dataE(data + len);
  for (; data < dataE; ++data)
    size = size << 8 | *data;
  return size;
};

bool ADTSFrame::parse(AP4_ByteStream *stream)
{
  uint8_t buffer[64];

  static const uint32_t freqTable[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };

  if (!AP4_SUCCEEDED(stream->Read(buffer, 2)))
    return false;

  m_outerHeader = static_cast<uint16_t>(getBE(buffer, 2));
  if ((m_outerHeader & 0xFFF6u) != 0xFFF0u)
    return false;

  m_innerHeaderSize = (m_outerHeader & 1) ? 7 : 5; // 16 bit CRC
  if (!AP4_SUCCEEDED(stream->Read(buffer, m_innerHeaderSize)))
    return false;

  m_innerHeader = getBE(buffer, m_innerHeaderSize);
  // add 0 crc to have bits on same place for crc / nocrc
  m_innerHeader <<= ((7 - m_innerHeaderSize) * 8);

  m_totalSize = (m_innerHeader >> 0x1D) & 0x1FFFu;
  m_frameCount = ((m_innerHeader >> 0x10) & 0x3u) ? 960 : 1024;
  m_summedFrameCount += m_frameCount;
  m_sampleRate = (m_innerHeader >> 0x32) & 0xFu;
  m_sampleRate = (m_sampleRate < 13) ? freqTable[m_sampleRate] : 0;
  m_channelConfig = (m_innerHeader >> 0x2E) & 0x7u;

  AP4_Position currentPos;
  stream->Tell(currentPos);
  stream->Seek(currentPos - (m_innerHeaderSize + 2));

  m_dataBuffer.SetDataSize(m_totalSize);
  if (!AP4_SUCCEEDED(stream->Read(m_dataBuffer.UseData(), m_dataBuffer.GetDataSize())))
    return false;

  //ADTS Streams have padding, at EOF
  AP4_Position pos, posNew;
  stream->Tell(pos);
  stream->Seek(pos + 16);
  stream->Tell(posNew);
  if (posNew - pos == 16)
    stream->Seek(pos);

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

bool ADTSReader::GetInformation(INPUTSTREAM_INFO &info)
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
