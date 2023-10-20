/*
 *  Copyright (C) 2016-2019 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <stdint.h>
#include <vector>

#include <bento4/Ap4Types.h>
#include <bento4/Ap4DataBuffer.h>

#include <kodi/addon-instance/Inputstream.h>
#include <webm/callback.h>
#include <webm/status.h>

class AP4_ByteStream;
class WebmAP4Reader;


class ATTR_DLL_LOCAL WebmReader : public webm::Callback
{
public:

  struct CUEPOINT
  {
    uint64_t pts;
    uint64_t duration;
    uint64_t pos_start;
    uint64_t pos_end;
  };

  WebmReader(AP4_ByteStream *stream);
  virtual ~WebmReader();

  void GetCuePoints(std::vector<CUEPOINT> &cuepoints);
  bool Initialize();

  void Reset();
  bool SeekTime(uint64_t timeInTs, bool preceeding);

  bool GetInformation(kodi::addon::InputstreamInfo& info);
  bool ReadPacket();

  webm::Status OnSegmentBegin(const webm::ElementMetadata& metadata, webm::Action* action) override;

  webm::Status OnElementBegin(const webm::ElementMetadata& metadata, webm::Action* action) override;
  webm::Status OnCuePoint(const webm::ElementMetadata& metadata, const webm::CuePoint& cue_point) override;

  webm::Status OnClusterBegin(const webm::ElementMetadata& metadata, const webm::Cluster& cluster, webm::Action* action) override;
  webm::Status OnSimpleBlockBegin(const webm::ElementMetadata& metadata, const webm::SimpleBlock& simple_block, webm::Action* action) override;
  webm::Status OnFrame(const webm::FrameMetadata& metadata, webm::Reader* reader, std::uint64_t* bytes_remaining) override;
  webm::Status OnTrackEntry(const webm::ElementMetadata& metadata, const webm::TrackEntry& track_entry) override;

  uint64_t GetDts() const { return m_pts; }
  uint64_t GetPts() const { return m_pts; }
  uint64_t GetDuration() const { return m_duration; }
  const AP4_Byte *GetPacketData() const { return m_frameBuffer.GetData(); }
  AP4_Size GetPacketSize() const { return m_frameBuffer.GetDataSize(); }
  uint64_t GetCueOffset()  const { return m_cueOffset; }

private:
  WebmAP4Reader *m_reader = nullptr;
  uint64_t m_cueOffset = 0;
  bool m_needFrame = false;
  uint64_t m_pts = STREAM_NOPTS_VALUE;
  uint64_t m_ptsOffset = 0;
  uint64_t m_duration = 0;
  std::vector<CUEPOINT> *m_cuePoints = nullptr;
  AP4_DataBuffer m_frameBuffer, m_codecPrivate;

  //Video section
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  std::string m_codecId;
  STREAMCODEC_PROFILE m_codecProfile = CodecProfileUnknown;
  bool m_metadataChanged = true;

#if INPUTSTREAM_VERSION_LEVEL > 0
  INPUTSTREAM_COLORSPACE m_colorSpace = INPUTSTREAM_COLORSPACE_UNSPECIFIED; /*!< @brief definition of colorspace */
  INPUTSTREAM_COLORRANGE m_colorRange = INPUTSTREAM_COLORRANGE_UNKNOWN;     /*!< @brief color range if available */
  INPUTSTREAM_COLORPRIMARIES m_colorPrimaries = INPUTSTREAM_COLORPRIMARY_UNSPECIFIED;
  INPUTSTREAM_COLORTRC m_colorTransferCharacteristic = INPUTSTREAM_COLORTRC_UNSPECIFIED;
  kodi::addon::InputstreamMasteringMetadata* m_masteringMetadata = nullptr;
  kodi::addon::InputstreamContentlightMetadata* m_contentLightMetadata = nullptr;
#endif
};
