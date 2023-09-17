/*
 *  Copyright (C) 2016-2019 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WebmReader.h"

#include "utils/StringUtils.h"
#include "utils/Utils.h"

#include <bento4/Ap4ByteStream.h>
#include <webm/reader.h>
#include <webm/webm_parser.h>

using namespace UTILS;

class ATTR_DLL_LOCAL WebmAP4Reader : public webm::Reader
{
public:
  WebmAP4Reader(AP4_ByteStream *stream) :m_stream(stream) {};

  webm::Status Run(webm::Callback *callback)
  {
    return m_parser.Feed(callback, this);
  }

  void Reset()
  {
    m_parser.DidSeek();
  }

  webm::Status Read(std::size_t num_to_read, std::uint8_t* buffer,
    std::uint64_t* num_actually_read) override
  {
    AP4_Size num_read;
    AP4_Result status = m_stream->ReadPartial(buffer, static_cast<AP4_Size>(num_to_read), num_read);
    *num_actually_read = num_read;

    if (AP4_SUCCEEDED(status))
    {
      if (num_to_read == num_read)
        return webm::Status(webm::Status::kOkCompleted);
      else if (num_read)
        return webm::Status(webm::Status::kOkPartial);
    }
    return webm::Status(webm::Status::kEndOfFile);
  }

  webm::Status Skip(std::uint64_t num_to_skip,
    std::uint64_t* num_actually_skipped) override
  {
    AP4_Position pos;
    if (AP4_FAILED(m_stream->Tell(pos) || m_stream->Seek(pos + num_to_skip)))
      return webm::Status(webm::Status::kEndOfFile);

    *num_actually_skipped = num_to_skip;
    return webm::Status(webm::Status::kOkCompleted);
  }

  std::uint64_t Position() const override
  {
    AP4_Position pos(0);
    if (AP4_FAILED(m_stream->Tell(pos)))
      return ~0ULL;
    return pos;
  }

private:
  AP4_ByteStream *m_stream;
  webm::WebmParser m_parser;
};

/*************************************************************/

WebmReader::WebmReader(AP4_ByteStream *stream)
  : m_reader(new WebmAP4Reader(stream))
{
}

void WebmReader::GetCuePoints(std::vector<CUEPOINT> &cuepoints)
{
  m_cuePoints = &cuepoints;
  m_reader->Reset();
  m_reader->Run(this);
}

bool WebmReader::Initialize()
{
  webm::Status status = m_reader->Run(this);
  return !status.is_parsing_error();
}

WebmReader::~WebmReader()
{
  delete m_reader, m_reader = nullptr;
#if INPUTSTREAM_VERSION_LEVEL > 0
  delete m_masteringMetadata, m_masteringMetadata = nullptr;
  delete m_contentLightMetadata, m_contentLightMetadata = nullptr;
#endif
}

void WebmReader::Reset()
{
  m_reader->Reset();
  m_needFrame = false;
}

bool WebmReader::GetInformation(kodi::addon::InputstreamInfo& info)
{
  if (!m_metadataChanged)
    return false;
  m_metadataChanged = false;

  bool ret = false;
  if (info.GetExtraData().empty() && m_codecPrivate.GetDataSize() > 0)
  {
    info.SetExtraData(m_codecPrivate.GetData(), m_codecPrivate.GetDataSize());
    ret = true;
  }

  if (!m_codecId.empty())
  {
    // Verify codec name for officially supported codecs only
    std::string codecName;
    if (STRING::Contains(m_codecId, "VP9"))
      codecName = CODEC::NAME_VP9;
    else if (STRING::Contains(m_codecId, "AV1"))
      codecName = CODEC::NAME_AV1;
    else if (STRING::Contains(m_codecId, "VORBIS"))
      codecName = CODEC::NAME_VORBIS;
    else if (STRING::Contains(m_codecId, "OPUS"))
      codecName = CODEC::NAME_OPUS;

    if (!codecName.empty() && info.GetCodecName() != codecName)
    {
      info.SetCodecName(codecName);
      ret = true;
    }
  }

  if (m_codecProfile && info.GetCodecProfile() != m_codecProfile)
    info.SetCodecProfile(m_codecProfile), ret = true;

  if (info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
  {
    if (m_width && m_width != info.GetWidth())
      info.SetWidth(m_width), ret = true;
    if (m_height && m_height != info.GetHeight())
      info.SetHeight(m_height), ret = true;
#if INPUTSTREAM_VERSION_LEVEL > 0
    if (info.GetColorSpace() != m_colorSpace)
      info.SetColorSpace(m_colorSpace), ret = true;
    if (info.GetColorRange() != m_colorRange)
      info.SetColorRange(m_colorRange), ret = true;
    if (info.GetColorPrimaries() != m_colorPrimaries)
      info.SetColorPrimaries(m_colorPrimaries), ret = true;
    if (info.GetColorTransferCharacteristic() != m_colorTransferCharacteristic)
      info.SetColorTransferCharacteristic(m_colorTransferCharacteristic), ret = true;

    if (m_masteringMetadata)
    {
      if (*m_masteringMetadata != info.GetMasteringMetadata())
        info.SetMasteringMetadata(*m_masteringMetadata), ret = true;

      if (*m_contentLightMetadata != info.GetContentLightMetadata())
        info.SetContentLightMetadata(*m_contentLightMetadata), ret = true;
    }
#endif
  }
  return ret;
}

// We assume that m_startpos is the current I-Frame position
bool WebmReader::SeekTime(uint64_t timeInTs, bool preceeding)
{
  Reset();
  return true;
}

bool WebmReader::ReadPacket()
{
  m_needFrame = true;
  m_reader->Run(this);

  return !m_needFrame;
}

/********************************************************************/

webm::Status WebmReader::OnSegmentBegin(const webm::ElementMetadata& metadata, webm::Action* action)
{
  m_cueOffset = metadata.position + metadata.header_size;
  return webm::Status(webm::Status::kOkCompleted);
}

webm::Status WebmReader::OnElementBegin(const webm::ElementMetadata& metadata, webm::Action* action)
{
  switch (metadata.id) {
  case webm::Id::kCues:
    if (m_cuePoints)
      *action = webm::Action::kRead;
    break;
  case webm::Id::kCluster:
    *action = webm::Action::kRead;
    break;
  case webm::Id::kTracks:
    *action = webm::Action::kRead;
    break;
  default:
    break;
  }
  return webm::Status(webm::Status::kOkCompleted);
}

webm::Status WebmReader::OnCuePoint(const webm::ElementMetadata& metadata, const webm::CuePoint& cue_point)
{
  if (m_cuePoints && cue_point.time.is_present()
    && !cue_point.cue_track_positions.empty())
  {
    CUEPOINT cue;
    cue.pts = cue_point.time.value();
    cue.duration = 0;
    //Attention these byte values are relative to Segment body start!
    cue.pos_start = cue_point.cue_track_positions[0].value().cluster_position.value();
    cue.pos_end = ~0ULL;

    if (!m_cuePoints->empty())
    {
      CUEPOINT &backcue = m_cuePoints->back();
      backcue.duration = cue.pts - backcue.pts;
      backcue.pos_end = cue.pos_start - 1;
    }
    m_cuePoints->push_back(cue);
  }

  return webm::Status(webm::Status::kOkCompleted);
}

webm::Status WebmReader::OnClusterBegin(const webm::ElementMetadata& metadata, const webm::Cluster& cluster, webm::Action* action)
{
  m_ptsOffset = cluster.timecode.is_present() ? cluster.timecode.value() : 0;
  *action = webm::Action::kRead;
  return webm::Status(webm::Status::kOkCompleted);
}

webm::Status WebmReader::OnSimpleBlockBegin(const webm::ElementMetadata& metadata, const webm::SimpleBlock& simple_block, webm::Action* action)
{
  if (!m_needFrame)
  {
    m_duration = (m_ptsOffset + simple_block.timecode) - m_pts;
    return webm::Status(webm::Status::kWouldBlock);
  }

  m_pts = m_ptsOffset + simple_block.timecode;
  *action = webm::Action::kRead;
  return webm::Status(webm::Status::kOkCompleted);
}

webm::Status WebmReader::OnFrame(const webm::FrameMetadata& metadata, webm::Reader* reader, std::uint64_t* bytes_remaining)
{
  m_needFrame = false;

  m_frameBuffer.SetDataSize(static_cast<AP4_Size>(*bytes_remaining));

  if (*bytes_remaining == 0)
    return webm::Status(webm::Status::kOkCompleted);

  webm::Status status;
  do {
    std::uint64_t num_actually_read;
    std::uint64_t num_read = 0;
    status = reader->Read(static_cast<size_t>(*bytes_remaining), m_frameBuffer.UseData() + num_read, &num_actually_read);
    *bytes_remaining -= num_actually_read;
    num_read += num_actually_read;
  } while (status.code == webm::Status::kOkPartial);

  return status;
}

webm::Status WebmReader::OnTrackEntry(const webm::ElementMetadata& metadata, const webm::TrackEntry& track_entry)
{
  if (track_entry.codec_id.is_present())
    m_codecId = track_entry.codec_id.value();

  if (track_entry.audio.is_present())
  {
    m_metadataChanged = true;

    if (track_entry.codec_private.is_present())
    {
      const auto& codecPrivateData = track_entry.codec_private.value();
      m_codecPrivate.SetData(codecPrivateData.data(),
                             static_cast<AP4_Size>(codecPrivateData.size()));
    }
  }
  else if (track_entry.video.is_present())
  {
    m_metadataChanged = true;

    const webm::Video &video = track_entry.video.value();

    m_width = static_cast<uint32_t>(video.pixel_width.is_present() ? video.pixel_width.value() : 0);
    m_height = static_cast<uint32_t>(video.pixel_height.is_present() ? video.pixel_height.value() : 0);

    if (track_entry.codec_private.is_present())
    {
      const auto& codecPrivateData = track_entry.codec_private.value();
      m_codecPrivate.SetData(codecPrivateData.data(),
                             static_cast<AP4_Size>(codecPrivateData.size()));
#if INPUTSTREAM_VERSION_LEVEL > 0
      if (codecPrivateData.size() > 3 && track_entry.codec_id.is_present() &&
          track_entry.codec_id.value() == "V_VP9")
      {
      m_codecProfile = static_cast<STREAMCODEC_PROFILE>(STREAMCODEC_PROFILE::VP9CodecProfile0 +
                                                        codecPrivateData[2]);
      }
#endif
    }

    if (video.colour.is_present())
    {
#if INPUTSTREAM_VERSION_LEVEL > 0
      if (video.colour.value().matrix_coefficients.is_present() && static_cast<uint64_t>(video.colour.value().matrix_coefficients.value()) < INPUTSTREAM_COLORSPACE_MAX)
        m_colorSpace = static_cast<INPUTSTREAM_COLORSPACE>(video.colour.value().matrix_coefficients.value());
      if (video.colour.value().range.is_present() && static_cast<uint64_t>(video.colour.value().range.value()) < INPUTSTREAM_COLORRANGE_MAX)
        m_colorRange = static_cast<INPUTSTREAM_COLORRANGE>(video.colour.value().range.value());
      if (video.colour.value().primaries.is_present() && static_cast<uint64_t>(video.colour.value().primaries.value()) < INPUTSTREAM_COLORTRC_MAX)
        m_colorPrimaries = static_cast<INPUTSTREAM_COLORPRIMARIES>(video.colour.value().primaries.value());
      if (video.colour.value().transfer_characteristics.is_present() && static_cast<uint64_t>(video.colour.value().transfer_characteristics.value()) < INPUTSTREAM_COLORTRC_MAX)
        m_colorTransferCharacteristic = static_cast<INPUTSTREAM_COLORTRC>(video.colour.value().transfer_characteristics.value());

      if (video.colour.value().mastering_metadata.is_present())
      {
        if (!m_masteringMetadata)
          m_masteringMetadata = new kodi::addon::InputstreamMasteringMetadata;
        if (!m_contentLightMetadata)
          m_contentLightMetadata = new kodi::addon::InputstreamContentlightMetadata;
        const webm::MasteringMetadata& mm = video.colour.value().mastering_metadata.value();
        m_masteringMetadata->SetLuminanceMax(mm.luminance_max.value());
        m_masteringMetadata->SetLuminanceMin(mm.luminance_min.value());
        m_masteringMetadata->SetPrimaryB_ChromaticityX(mm.primary_b_chromaticity_x.value());
        m_masteringMetadata->SetPrimaryB_ChromaticityY(mm.primary_b_chromaticity_y.value());
        m_masteringMetadata->SetPrimaryG_ChromaticityX(mm.primary_g_chromaticity_x.value());
        m_masteringMetadata->SetPrimaryG_ChromaticityY(mm.primary_g_chromaticity_y.value());
        m_masteringMetadata->SetPrimaryR_ChromaticityX(mm.primary_r_chromaticity_x.value());
        m_masteringMetadata->SetPrimaryR_ChromaticityY(mm.primary_r_chromaticity_y.value());
        m_masteringMetadata->SetWhitePoint_ChromaticityX(mm.white_point_chromaticity_x.value());
        m_masteringMetadata->SetWhitePoint_ChromaticityY(mm.white_point_chromaticity_y.value());

        m_contentLightMetadata->SetMaxCll(video.colour.value().max_cll.is_present()
                                              ? video.colour.value().max_cll.value()
                                              : 1000);
        m_contentLightMetadata->SetMaxFall(video.colour.value().max_fall.is_present()
                                               ? video.colour.value().max_fall.value()
                                               : 200);
      }
#endif
    }
  }
  return webm::Status(webm::Status::kOkCompleted);
}
