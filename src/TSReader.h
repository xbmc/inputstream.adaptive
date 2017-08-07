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
#include <vector>
#include "../lib/mpegts/tsDemuxer.h"
#include "Ap4Types.h"
#include <kodi/addon-instance/Inputstream.h>

class AP4_ByteStream;

class TSReader : public TSDemux::TSDemuxer
{
public:
  TSReader(AP4_ByteStream *stream);
  virtual ~TSReader();

  bool Initialize();

  virtual const unsigned char* ReadAV(uint64_t pos, size_t len) override;

  void Reset(bool resetPackets = true);
  bool StartStreaming(AP4_UI32 typeMask);
  bool SeekTime(uint64_t timeInTs);
  void SetPTSOffset(uint64_t offset) { m_PTSOffset = offset; };

  bool GetInformation(INPUTSTREAM_INFO &info);
  bool ReadPacket(bool streamInfo = false);

  uint64_t GetDts() const { return m_pkt.dts == PTS_UNSET ? PTS_UNSET : m_pkt.dts; }
  uint64_t GetPts() const { return m_pkt.pts == PTS_UNSET ? PTS_UNSET : m_pkt.pts; }
  uint64_t GetDuration() const { return m_pkt.duration; }
  int64_t GetPTSDiff() const { return m_PTSDiff; }
  const AP4_Byte *GetPacketData() const { return m_pkt.data; };
  const AP4_Size GetPacketSize() const { return m_pkt.size; };
  const INPUTSTREAM_INFO::STREAM_TYPE GetStreamType() const;

private:
  bool GetPacket();
  bool HandleProgramChange();
  bool HandleStreamChange(uint16_t pid);

  TSDemux::AVContext* m_AVContext;

  AP4_ByteStream *m_stream;

  TSDemux::STREAM_PKT m_pkt;
  uint64_t m_startPos;
  uint64_t m_PTSOffset;
  int64_t m_PTSDiff;

  struct TSINFO
  {
    TSINFO(TSDemux::ElementaryStream* stream) : m_stream(stream), m_needInfo(true), m_changed(false), m_enabled(false), m_streamType(INPUTSTREAM_INFO::TYPE_NONE) {};

    TSDemux::ElementaryStream* m_stream;
    bool m_needInfo, m_changed, m_enabled;
    INPUTSTREAM_INFO::STREAM_TYPE m_streamType;
  };
  std::vector<TSINFO> m_streamInfos;
};