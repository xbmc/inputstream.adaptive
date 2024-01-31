/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SubtitleSampleReader.h"

#include "../Session.h"
#include "../utils/FFmpeg.h"
#include "../utils/MemUtils.h"
#include "../utils/UrlUtils.h"
#include "../utils/log.h"

#include <kodi/Filesystem.h>

using namespace UTILS;

namespace
{
// This struct must match the same on:
// xbmc/cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlayCodec.h
struct SubtitlePacketExtraData
{
  double m_chapterStartTime;
};
} // unnamed namespace

CSubtitleSampleReader::CSubtitleSampleReader(
    std::string url,
    AP4_UI32 streamId,
    const std::string& codecInternalName,
    std::string_view streamParams,
    const std::map<std::string, std::string>& streamHeaders)
  : m_streamId{streamId}
{
  // Append stream parameters, only if not already provided
  if (url.find('?') == std::string::npos)
    URL::AppendParameters(url, streamParams);

  // open the file
  kodi::vfs::CFile file;
  if (!file.CURLCreate(url))
    return;

  for (auto& header : streamHeaders)
  {
    file.CURLAddOption(ADDON_CURL_OPTION_HEADER, header.first, header.second);
  }

  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "seekable", "0");
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "acceptencoding", "gzip");
  file.CURLOpen(ADDON_READ_CHUNKED | ADDON_READ_NO_CACHE);

  AP4_DataBuffer result;

  // read the file
  static const unsigned int CHUNKSIZE = 16384;
  AP4_Byte buf[CHUNKSIZE];
  size_t nbRead;
  while ((nbRead = file.Read(buf, CHUNKSIZE)) > 0 && ~nbRead)
    result.AppendData(buf, nbRead);
  file.Close();

  // Single subtitle file
  if (codecInternalName == "wvtt")
    m_codecHandler = std::make_unique<WebVTTCodecHandler>(nullptr, true);
  else
    m_codecHandler = std::make_unique<TTMLCodecHandler>(nullptr);

  m_codecHandler->Transform(0, 0, result, 1000);
}

CSubtitleSampleReader::CSubtitleSampleReader(SESSION::CStream* stream,
                                             AP4_UI32 streamId,
                                             const std::string& codecInternalName)
  : m_streamId{streamId}, m_adByteStream{stream->GetAdByteStream()}, m_adStream{&stream->m_adStream}
{
  // Segmented subtitle
  if (codecInternalName == "wvtt")
  {
    m_isSideDataRequired = true;
    m_codecHandler = std::make_unique<WebVTTCodecHandler>(nullptr, false);
  }
  else
  {
    m_codecHandler = std::make_unique<TTMLCodecHandler>(nullptr);
  }
}

AP4_Result CSubtitleSampleReader::Start(bool& bStarted)
{
  m_eos = false;
  if (m_started)
    return AP4_SUCCESS;

  m_started = true;
  return AP4_SUCCESS;
}

AP4_Result CSubtitleSampleReader::ReadSample()
{
  if (m_codecHandler->ReadNextSample(m_sample,
                                     m_sampleData)) // Read the sample data from a file url
  {
    m_pts = (m_sample.GetCts() * 1000);
    return AP4_SUCCESS;
  }
  else if (m_adByteStream) // Read the sample data from a segment file stream (e.g. HLS)
  {
    // Get the next segment
    if (m_adStream && m_adStream->ensureSegment())
    {
      size_t segSize;
      if (m_adStream->retrieveCurrentSegmentBufferSize(segSize))
      {
        AP4_DataBuffer segData;
        while (segSize > 0)
        {
          AP4_Size readSize = m_segmentChunkSize;
          if (segSize < static_cast<size_t>(m_segmentChunkSize))
            readSize = static_cast<AP4_Size>(segSize);

          AP4_Byte* buf = new AP4_Byte[readSize];
          segSize -= readSize;
          if (AP4_SUCCEEDED(m_adByteStream->Read(buf, readSize)))
          {
            segData.AppendData(buf, readSize);
            delete[] buf;
          }
          else
          {
            delete[] buf;
            break;
          }
        }
        auto rep = m_adStream->getRepresentation();
        if (rep)
        {
          auto currentSegment = rep->current_segment_;
          if (currentSegment)
          {
            m_codecHandler->Transform(currentSegment->startPTS_,
                                      currentSegment->m_duration, segData, 1000);
            if (m_codecHandler->ReadNextSample(m_sample, m_sampleData))
            {
              m_pts = m_sample.GetCts();
              return AP4_SUCCESS;
            }
          }
          else
            LOG::LogF(LOGERROR, "Failed to get current segment of subtitle stream");
        }
        else
          LOG::LogF(LOGERROR, "Failed to get Representation of subtitle stream");
      }
      else
      {
        LOG::LogF(LOGWARNING, "Failed to get subtitle segment buffer size");
      }
    }
  }
  m_eos = true;
  return AP4_ERROR_EOS;
}

void CSubtitleSampleReader::Reset(bool bEOS)
{
  if (m_adByteStream || bEOS)
  {
    m_sampleData.SetDataSize(0);
    m_eos = bEOS;
    m_codecHandler->Reset();
  }
}

bool CSubtitleSampleReader::GetInformation(kodi::addon::InputstreamInfo& info)
{
  if (m_codecHandler->m_extraData.GetDataSize() &&
      !info.CompareExtraData(m_codecHandler->m_extraData.GetData(),
                             m_codecHandler->m_extraData.GetDataSize()))
  {
    info.SetExtraData(m_codecHandler->m_extraData.GetData(),
                      m_codecHandler->m_extraData.GetDataSize());
    return true;
  }
  return false;
}

bool CSubtitleSampleReader::TimeSeek(uint64_t pts, bool preceeding)
{
  if (dynamic_cast<WebVTTCodecHandler*>(m_codecHandler.get()))
  {
    m_pts = pts;
    return true;
  }
  else
  {
    if (m_codecHandler->TimeSeek(pts / 1000))
      return AP4_SUCCEEDED(ReadSample());
    return false;
  }
}

void CSubtitleSampleReader::SetPTSDiff(uint64_t pts)
{
  // Its needed set the PTS diff from the timing stream
  // to allow sync segmented subtitles for cases like
  // HLS with multiple periods
  m_ptsDiff = pts;
}

void CSubtitleSampleReader::SetDemuxPacketSideData(DEMUX_PACKET* pkt,
                                                   std::shared_ptr<SESSION::CSession> session)
{
  if (!m_isSideDataRequired || !pkt)
    return;

  pkt->pSideData = MEMORY::AlignedMalloc(sizeof(struct UTILS::FFMPEG::AVPacketSideData));
  if (!pkt->pSideData)
  {
    LOG::Log(LOGERROR, "Cannot allocate AVPacketSideData");
    return;
  }
  void* subPktDataPtr{MEMORY::AlignedMalloc(sizeof(struct SubtitlePacketExtraData))};
  if (!subPktDataPtr)
  {
    MEMORY::AlignedFree(pkt->pSideData);
    pkt->pSideData = nullptr;
    LOG::Log(LOGERROR, "Cannot allocate SubtitlePacketExtraData");
    return;
  }

  auto subPktData{reinterpret_cast<SubtitlePacketExtraData*>(subPktDataPtr)};
  // HSL multi-period require to sync the cues timestamps of Segmented WebVTT
  // with the current chapter start time (period start)
  // so we have to provide the chapter start time to Kodi subtitle parser
  subPktData->m_chapterStartTime = session->GetChapterStartTime();

  auto avpList{static_cast<UTILS::FFMPEG::AVPacketSideData*>(pkt->pSideData)};
  avpList[0].data = reinterpret_cast<uint8_t*>(subPktDataPtr);
  avpList[0].type = UTILS::FFMPEG::AV_PKT_DATA_NEW_EXTRADATA;
  avpList[0].size = sizeof(struct SubtitlePacketExtraData);

  pkt->iSideDataElems = 1;
}
