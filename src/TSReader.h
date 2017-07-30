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
#include "Ap4DataBuffer.h"
#include <kodi/addon-instance/Inputstream.h>

class AP4_ByteStream;

class TSReader : public TSDemux::TSDemuxer
{
public:
  TSReader(AP4_ByteStream *stream);
  virtual ~TSReader();

  virtual const unsigned char* ReadAV(uint64_t pos, size_t len) override;

  void Reset();
  bool StartStreaming(AP4_UI32 typeMask);
  bool GetInformation(INPUTSTREAM_INFO &info);
  uint32_t GetTimeScale() const { return 90000; };
  bool ReadPacket(bool streamInfo = false);

  uint64_t GetDts() const { return m_pkt.dts; }
  uint64_t GetPts() const { return m_pkt.pts; }
  uint64_t GetDuration() const { return m_pkt.duration; }
  const AP4_Byte *GetPacketData() const { return m_pkt.data; };
  const AP4_Size GetPacketSize() const { return m_pkt.size; };

private:
  bool GetPacket();
  bool HandleProgramChange();
  bool HandleStreamChange(uint16_t pid);

  TSDemux::AVContext* m_AVContext;

  AP4_ByteStream *m_stream;

  TSDemux::STREAM_PKT m_pkt;
  AP4_DataBuffer m_readBuffer;

  struct TSINFO
  {
    TSINFO(TSDemux::ElementaryStream* stream) : m_stream(stream), m_needInfo(true), m_changed(false), m_streamType(INPUTSTREAM_INFO::TYPE_NONE) {};

    TSDemux::ElementaryStream* m_stream;
    bool m_needInfo, m_changed;
    INPUTSTREAM_INFO::STREAM_TYPE m_streamType;
  };
  std::vector<TSINFO> m_streamInfos;
};