/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SubtitleSampleReader.h"

#include "CompKodiProps.h"
#include "CompResources.h"
#include "SrvBroker.h"
#include "Stream.h"
#include "AdaptiveByteStream.h"
#include "codechandler/TTMLCodecHandler.h"
#include "codechandler/WebVTTCodecHandler.h"
#include "common/AdaptiveStream.h"
#include "common/AdaptiveTree.h"
#include "common/Representation.h"
#include "utils/CurlUtils.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

using namespace PLAYLIST;
using namespace UTILS;

bool CSubtitleSampleReader::Initialize(SESSION::CStream* stream)
{

  std::string codecInternalName = stream->m_info.GetCodecInternalName();
  const CRepresentation* repr = stream->m_adStream.getRepresentation();

  if (repr->IsSubtitleFileStream())
  {
    // Single "sidecar" subtitle file (for entire video duration)
    if (STRING::Contains(codecInternalName, CODEC::FOURCC_WVTT))
      m_codecHandler = std::make_unique<WebVTTCodecHandler>(nullptr, true);
    else if (STRING::Contains(codecInternalName, CODEC::FOURCC_TTML) ||
             STRING::Contains(codecInternalName, CODEC::FOURCC_STPP))
      m_codecHandler = std::make_unique<TTMLCodecHandler>(nullptr, true);
    else
    {
      LOG::LogF(LOGERROR, "Codec \"%s\" not implemented", codecInternalName.data());
      return false;
    }

    return InitializeFile(repr->GetBaseUrl());
  }
  else
  {
    // Segmented subtitle
    m_adByteStream = stream->GetAdByteStream();
    m_adStream = &stream->m_adStream;

    if (STRING::Contains(codecInternalName, CODEC::FOURCC_WVTT))
      m_codecHandler = std::make_unique<WebVTTCodecHandler>(nullptr, false);
    else if (STRING::Contains(codecInternalName, CODEC::FOURCC_TTML) ||
             STRING::Contains(codecInternalName, CODEC::FOURCC_DFXP) ||
             STRING::Contains(codecInternalName, CODEC::FOURCC_STPP))
      m_codecHandler = std::make_unique<TTMLCodecHandler>(nullptr, false);
    else
    {
      LOG::LogF(LOGERROR, "Codec \"%s\" not implemented", codecInternalName.data());
      return false;
    }
    return true;
  }
}

bool CSubtitleSampleReader::InitializeFile(std::string url)
{
  auto kodiProps = CSrvBroker::GetKodiProps();

  // Append stream parameters
  URL::AppendParameters(url, kodiProps.GetStreamParams());

  // Download the file
  CURL::CUrl curl(url);
  curl.AddHeaders(kodiProps.GetStreamHeaders());
  int statusCode = curl.Open();
  if (statusCode == -1)
  {
    LOG::Log(LOGERROR, "Download failed, internal error: %s", url.c_str());
    return false;
  }
  else if (statusCode >= 400)
  {
    LOG::Log(LOGERROR, "Download failed, HTTP error %d: %s", statusCode, url.c_str());
    return false;
  }

  std::string data;

  if (curl.Read(data) != CURL::ReadStatus::IS_EOF)
  {
    LOG::Log(LOGERROR, "Download failed: %s", statusCode, url.c_str());
    return false;
  }

  if (!data.empty())
  {
    AP4_DataBuffer buffer{data.c_str(), static_cast<AP4_Size>(data.size())};
    m_codecHandler->Transform(0, 0, buffer, 1000);
  }
  return true;
}

AP4_Result CSubtitleSampleReader::Start(bool& bStarted)
{
  if (!m_codecHandler)
  {
    m_eos = true;
    return AP4_FAILURE;
  }

  if (m_started)
    return AP4_SUCCESS;

  m_started = true;
  return ReadSample();
}

bool CSubtitleSampleReader::IsReady()
{
  // The reader is ready to process data when there is:
  // 1) single subtitles file (there is no m_adByteStream)
  // 2) segmented subtitles, and its not waiting for segments,
  //    it need to wait for the next manifest live update to get new segments (like HLS)
  return !m_adByteStream ||
         (m_adByteStream && m_adStream && !m_adStream->getRepresentation()->IsWaitForSegment());
}

AP4_Result CSubtitleSampleReader::ReadSample()
{
  m_sampleData.SetDataSize(0);

  if (m_codecHandler->ReadNextSample(m_sample,
                                     m_sampleData)) // Read the sample data from a file url
  {
    m_pts = m_sample.GetCts() * 1000 + m_ptsOffset;
    return AP4_SUCCESS;
  }
  else if (m_adByteStream && m_adStream) // Read the sample data from a segment file stream (e.g. HLS)
  {
    // Get the next segment
    std::vector<uint8_t> buffer;
    if (m_adByteStream->ReadFull(buffer))
    {
      if (buffer.empty()) // No data, more likely due to download error
      {
        LOG::LogF(LOGWARNING, "No buffer segment data from subtitle stream");
        return AP4_ERROR_READ_FAILED;
      }

      auto rep = m_adStream->getRepresentation();
      if (rep)
      {
        auto& currentSegment = rep->current_segment_;
        if (currentSegment.has_value())
        {
          AP4_DataBuffer segData(buffer.data(), static_cast<AP4_Size>(buffer.size()));
          uint64_t segDur = currentSegment->m_endPts - currentSegment->startPTS_;

          AP4_UI32 duration =
              static_cast<AP4_UI32>((segDur * STREAM_TIME_BASE) / rep->GetTimescale());

          const AP4_UI64 pts = (currentSegment->startPTS_ * STREAM_TIME_BASE) / rep->GetTimescale();

          m_codecHandler->Transform(pts, duration, segData, 1000);
          if (m_codecHandler->ReadNextSample(m_sample, m_sampleData))
          {
            // m_ptsOffset is intentionally not applied here — segmented subtitle PTS is already
            // absolute, derived from the segment's own startPTS_ passed to Transform above.
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
    else if (m_adStream->getRepresentation()->IsWaitForSegment())
    {
      // Wait for manifest live update to get next segment
      return AP4_SUCCESS;
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
    m_pts = 0;
  }
}

bool CSubtitleSampleReader::GetInformation(kodi::addon::InputstreamInfo& info)
{
  if (!m_codecHandler->m_extraData.empty() &&
      !info.CompareExtraData(m_codecHandler->m_extraData.data(),
                             m_codecHandler->m_extraData.size()))
  {
    info.SetExtraData(m_codecHandler->m_extraData);
    return true;
  }
  return false;
}

bool CSubtitleSampleReader::TimeSeek(uint64_t pts)
{
  if (dynamic_cast<WebVTTCodecHandler*>(m_codecHandler.get()))
  {
    return true;
  }
  else
  {
    if (m_codecHandler->TimeSeek(pts / 1000))
      return AP4_SUCCEEDED(ReadSample());
    return false;
  }
}
