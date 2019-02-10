/*
*      Copyright (C) 2016 - 2019 peak3d
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

#include "WebmReader.h"
#include "Ap4ByteStream.h"

#include <webm/reader.h>
#include <webm/webm_parser.h>

class WebmAP4Reader : public webm::Reader
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
    AP4_Result status = m_stream->ReadPartial(buffer, num_to_read, num_read);
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
}

void WebmReader::Reset()
{
  m_reader->Reset();
  m_needFrame = false;
}

bool WebmReader::GetInformation(INPUTSTREAM_INFO &info)
{
  if (!info.m_ExtraSize && m_codecPrivate.GetDataSize() > 0)
  {
    info.m_ExtraSize = m_codecPrivate.GetDataSize();
    info.m_ExtraData = static_cast<const uint8_t*>(malloc(info.m_ExtraSize));
    memcpy(const_cast<uint8_t*>(info.m_ExtraData), m_codecPrivate.GetData(), info.m_ExtraSize);
    return true;
  }
  return false;
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

  m_frameBuffer.SetDataSize(*bytes_remaining);

  if (*bytes_remaining == 0)
    return webm::Status(webm::Status::kOkCompleted);

  webm::Status status;
  do {
    std::uint64_t num_actually_read;
    std::uint64_t num_read = 0;
    status = reader->Read(*bytes_remaining, m_frameBuffer.UseData() + num_read, &num_actually_read);
    *bytes_remaining -= num_actually_read;
    num_read += num_actually_read;
  } while (status.code == webm::Status::kOkPartial);

  return status;
}

webm::Status WebmReader::OnTrackEntry(const webm::ElementMetadata& metadata, const webm::TrackEntry& track_entry)
{
  if (track_entry.video.is_present())
  {
    const webm::Video &video = track_entry.video.value();

    m_width = static_cast<uint32_t>(video.pixel_width.is_present() ? video.pixel_width.value() : 0);
    m_height = static_cast<uint32_t>(video.pixel_height.is_present() ? video.pixel_height.value() : 0);

    if (track_entry.codec_private.is_present())
      m_codecPrivate.SetData(track_entry.codec_private.value().data(), track_entry.codec_private.value().size());
  }
  return webm::Status(webm::Status::kOkCompleted);
}
