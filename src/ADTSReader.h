/*
 *  Copyright (C) 2017 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <stdint.h>
#include <bento4/Ap4Types.h>
#include <bento4/Ap4DataBuffer.h>
#include <kodi/addon-instance/Inputstream.h>

// Forwards
class AP4_ByteStream;

namespace adaptive
{
enum class AdtsType;
}

class ATTR_DLL_LOCAL ID3TAG
{
public:
  enum PARSECODE
  {
    PARSE_SUCCESS,
    PARSE_FAIL,
    PARSE_NO_ID3
  };

  PARSECODE parse(AP4_ByteStream* stream);
  void SkipID3Data(AP4_ByteStream* stream);
  bool getPts(uint64_t &pts) { if (m_timestamp) { pts = m_timestamp; m_timestamp = 0; return true; } return false; }

private:
  static uint64_t getSize(const uint8_t *data, unsigned int size, unsigned int shift);

  static const unsigned int HEADER_SIZE = 10;

  uint8_t m_majorVer;
  uint8_t m_revisionVer;
  uint8_t m_flags;
  uint64_t m_timestamp;
};


class ATTR_DLL_LOCAL ADTSFrame
{
public:
  /*! \brief Adjust the stream position to advance over padding if neccessary (end of file)
   *  \param stream The stream to check
   */
  void AdjustStreamForPadding(AP4_ByteStream* stream);
  adaptive::AdtsType GetAdtsType(AP4_ByteStream* stream);
  bool parse(AP4_ByteStream *stream);
  bool ParseAac(AP4_ByteStream* stream);
  bool ParseAc3(AP4_ByteStream* stream);
  bool ParseEc3(AP4_ByteStream* stream);
  void reset() { m_summedFrameCount = 0; m_frameCount = 0; m_dataBuffer.SetDataSize(0); }
  void resetFrameCount() { m_summedFrameCount = 0; }
  uint64_t getPtsOffset() const { return m_sampleRate ? (static_cast<uint64_t>(m_summedFrameCount) * 90000) / m_sampleRate : 0; }
  uint64_t getDuration() const { return m_sampleRate ? (static_cast<uint64_t>(m_frameCount) * 90000) / m_sampleRate : 0; }
  const AP4_Byte *getData() const { return m_dataBuffer.GetData(); }
  AP4_Size getDataSize() const { return m_dataBuffer.GetDataSize(); }
private:
  uint32_t m_totalSize = 0;
  uint32_t m_summedFrameCount = 0;
  uint32_t m_frameCount = 0;
  uint32_t m_sampleRate = 0;
  uint32_t m_channelCount = 0;

  AP4_DataBuffer m_dataBuffer;
};

class ATTR_DLL_LOCAL ADTSReader
{
public:
  ADTSReader(AP4_ByteStream *stream);
  virtual ~ADTSReader();

  void Reset();
  bool SeekTime(uint64_t timeInTs, bool preceeding);

  bool GetInformation(kodi::addon::InputstreamInfo& info);
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
  uint64_t m_basePts{0};
  uint64_t m_pts;
};
