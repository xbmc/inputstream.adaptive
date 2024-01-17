/*
 *  Copyright (C) 2017 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "mpegts/tsDemuxer.h"

#include <stdint.h>
#include <vector>

#include <bento4/Ap4Types.h>
#include <kodi/addon-instance/Inputstream.h>

class AP4_ByteStream;

class ATTR_DLL_LOCAL TSReader : public TSDemux::TSDemuxer
{
public:
  TSReader(AP4_ByteStream *stream, uint32_t requiredMask);
  virtual ~TSReader();

  bool Initialize();

  virtual bool ReadAV(uint64_t pos, unsigned char * data, size_t len) override;

  void Reset(bool resetPackets = true);
  bool StartStreaming(AP4_UI32 typeMask);
  bool SeekTime(uint64_t timeInTs, bool preceeding);

  bool GetInformation(kodi::addon::InputstreamInfo& info);
  bool ReadPacket(bool streamInfo = false);

  uint64_t GetDts() const { return m_pkt.dts == PTS_UNSET ? PTS_UNSET : m_pkt.dts; }
  uint64_t GetPts() const { return m_pkt.pts == PTS_UNSET ? PTS_UNSET : m_pkt.pts; }
  uint64_t GetDuration() const { return m_pkt.duration; }
  const AP4_Byte *GetPacketData() const { return m_pkt.data; };
  const AP4_Size GetPacketSize() const { return static_cast<AP4_Size>(m_pkt.size); }
  const INPUTSTREAM_TYPE GetStreamType() const;

private:
  bool GetPacket();
  bool HandleProgramChange();
  bool HandleStreamChange(uint16_t pid);

  TSDemux::AVContext* m_AVContext;

  AP4_ByteStream *m_stream;

  TSDemux::STREAM_PKT m_pkt;
  AP4_Position m_startPos;
  uint32_t m_requiredMask;
  uint32_t m_typeMask;

  struct TSINFO
  {
    TSINFO(TSDemux::ElementaryStream* stream) : m_stream(stream), m_needInfo(true), m_changed(false), m_enabled(false), m_streamType(INPUTSTREAM_TYPE_NONE) {};

    TSDemux::ElementaryStream* m_stream;
    bool m_needInfo, m_changed, m_enabled;
    INPUTSTREAM_TYPE m_streamType;
  };
  std::vector<TSINFO> m_streamInfos;
};
