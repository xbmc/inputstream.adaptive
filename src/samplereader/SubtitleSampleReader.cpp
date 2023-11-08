/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SubtitleSampleReader.h"

#include "CompKodiProps.h"
#include "SrvBroker.h"
#include "Stream.h"
#include "AdaptiveByteStream.h"
#include "codechandler/TTMLCodecHandler.h"
#include "codechandler/WebVTTCodecHandler.h"
#include "common/AdaptiveStream.h"
#include "common/Representation.h"
#include "utils/CurlUtils.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

using namespace PLAYLIST;
using namespace UTILS;

CSubtitleSampleReader::CSubtitleSampleReader(AP4_UI32 streamId) : m_streamId{streamId}
{
}

bool CSubtitleSampleReader::Initialize(SESSION::CStream* stream)
{

  std::string codecInternalName = stream->m_info.GetCodecInternalName();
  const CRepresentation* repr = stream->m_adStream.getRepresentation();

  if (repr->IsSubtitleFileStream())
  {
    // Single subtitle file (for entire video duration)
    if (STRING::Contains(codecInternalName, CODEC::FOURCC_WVTT))
      m_codecHandler = std::make_unique<WebVTTCodecHandler>(nullptr, true);
    else if (STRING::Contains(codecInternalName, CODEC::FOURCC_TTML))
      m_codecHandler = std::make_unique<TTMLCodecHandler>(nullptr);
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
    else if (STRING::Contains(codecInternalName, CODEC::FOURCC_TTML))
      m_codecHandler = std::make_unique<TTMLCodecHandler>(nullptr);
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

  // Append stream parameters, only if not already provided
  if (url.find('?') == std::string::npos)
    URL::AppendParameters(url, kodiProps.GetStreamParams());

  // Download the file
  CURL::CUrl curl(url);
  curl.AddHeaders(kodiProps.GetStreamHeaders());
  int statusCode = curl.Open(true);
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

  m_eos = false;
  if (m_started)
    return AP4_SUCCESS;

  m_started = true;
  m_pts = GetStartPTS();
  m_ptsDiff = GetStartPTS();
  return AP4_SUCCESS;
}

AP4_Result CSubtitleSampleReader::ReadSample()
{
  if (m_codecHandler->ReadNextSample(m_sample,
                                     m_sampleData)) // Read the sample data from a file url
  {
    m_pts = (m_sample.GetCts() * 1000) + GetStartPTS();
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
            m_codecHandler->Transform(currentSegment->startPTS_ + GetStartPTS(),
                                      static_cast<AP4_UI32>(currentSegment->m_duration), segData,
                                      1000);
            if (m_codecHandler->ReadNextSample(m_sample, m_sampleData))
            {
              m_pts = m_sample.GetCts();
              m_ptsDiff = m_pts - m_ptsOffset;
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
