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
  enum CODEC_FLAG
  {
    CODEC_FLAG_NONE = 0,
    CODEC_FLAG_ATMOS = 1 << 0
  };

  // \brief Generic frame info struct for all codec types.
  struct ADTSFrameInfo
  {
    adaptive::AdtsType m_codecType{0};
    int m_codecProfile{-1}; // For his definition refer to the type of codec parsed, -1 for unset value
    int m_codecFlags{CODEC_FLAG_NONE}; // Refer to CODEC_FLAG enum
    AP4_Size m_frameSize{0};
    uint32_t m_frameCount{0};
    uint32_t m_sampleRate{0};
    uint32_t m_channels{0};
  };

  /*!
   * \brief Adjust the stream position to advance over padding if neccessary (end of file)
   * \param stream The stream to check
   */
  void AdjustStreamForPadding(AP4_ByteStream* stream);
  ADTSFrameInfo GetFrameInfo(AP4_ByteStream* stream);
  bool parse(AP4_ByteStream *stream);
  bool ParseAac(AP4_ByteStream* stream);
  bool ParseAacHeader(AP4_ByteStream* stream, ADTSFrameInfo& frameInfo);
  bool ParseAc3(AP4_ByteStream* stream);
  bool ParseAc3Header(AP4_ByteStream* stream, ADTSFrameInfo& frameInfo);
  bool ParseEc3(AP4_ByteStream* stream);
  bool ParseEc3Header(AP4_ByteStream* stream, ADTSFrameInfo& frameInfo);
  void reset();
  void resetFrameCount() { m_summedFrameCount = 0; }
  uint64_t getPtsOffset() const;
  uint64_t getDuration() const;
  const AP4_Byte *getData() const { return m_dataBuffer.GetData(); }
  AP4_Size getDataSize() const { return m_dataBuffer.GetDataSize(); }

private:
  uint64_t m_summedFrameCount{0};
  ADTSFrameInfo m_frameInfo;
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

  static const uint64_t ADTS_PTS_UNSET = 0x1ffffffffULL;

private:
  AP4_ByteStream *m_stream;
  ID3TAG m_id3TagParser;
  ADTSFrame m_frameParser;
  uint64_t m_basePts{0};
  uint64_t m_pts{0};
};
