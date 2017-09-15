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

#pragma once

#include <stdint.h>
#include "Ap4Types.h"
#include "Ap4DataBuffer.h"
#include <kodi/addon-instance/Inputstream.h>

class AP4_ByteStream;

class ID3TAG
{
public:
  enum PARSECODE
  {
    PARSE_SUCCESS,
    PARSE_FAIL,
    PARSE_NO_ID3
  };

  PARSECODE parse(AP4_ByteStream *stream);
  bool getPts(uint64_t &pts) { if (m_timestamp) { pts = m_timestamp; m_timestamp = 0; return true; } return false; }

private:
  static uint64_t getSize(const uint8_t *data, unsigned int size, unsigned int shift);

  static const unsigned int HEADER_SIZE = 10;

  uint8_t m_majorVer;
  uint8_t m_flags;
  uint64_t m_timestamp;
};


class ADTSFrame
{
public:
  bool parse(AP4_ByteStream *stream);
  void reset() { m_summedFrameCount = 0; m_frameCount = 0; m_dataBuffer.SetDataSize(0); }
  void resetFrameCount() { m_summedFrameCount = 0; }
  uint64_t getPtsOffset() const { return m_sampleRate ? (static_cast<uint64_t>(m_summedFrameCount) * 90000) / m_sampleRate : 0; }
  uint64_t getDuration() const { return m_sampleRate ? (static_cast<uint64_t>(m_frameCount) * 90000) / m_sampleRate : 0; }
  const AP4_Byte *getData() const { return m_dataBuffer.GetData(); }
  AP4_Size getDataSize() const { return m_dataBuffer.GetDataSize(); }
private:
  uint64_t getBE(const uint8_t *data, unsigned int len);
  uint16_t m_outerHeader;
  uint64_t m_innerHeader;
  long m_innerHeaderSize;

  uint32_t m_totalSize = 0;
  uint32_t m_summedFrameCount = 0;
  uint32_t m_frameCount = 0;
  uint32_t m_sampleRate = 0;
  uint32_t m_channelConfig = 0;

  AP4_DataBuffer m_dataBuffer;
};

class ADTSReader
{
public:
  ADTSReader(AP4_ByteStream *stream);
  virtual ~ADTSReader();

  void Reset();
  bool SeekTime(uint64_t timeInTs, bool preceeding);

  bool GetInformation(INPUTSTREAM_INFO &info);
  bool ReadPacket();

  uint64_t GetPts() const { return m_pts; }
  uint64_t GetDuration() const { return m_frameParser.getDuration(); }
  const AP4_Byte *GetPacketData() const { return m_frameParser.getData(); };
  const AP4_Size GetPacketSize() const { return m_frameParser.getDataSize(); };

private:
  static const uint64_t ADTS_PTS_UNSET = 0x1ffffffffULL;
  AP4_ByteStream *m_stream;
  ID3TAG m_id3TagParser;
  ADTSFrame m_frameParser;
  uint64_t m_basePts, m_pts;
};