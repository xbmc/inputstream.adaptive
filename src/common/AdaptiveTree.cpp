/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AdaptiveTree.h"
#include "Chooser.h"

#include "../utils/FileUtils.h"
#include "../utils/StringUtils.h"
#include "../utils/UrlUtils.h"
#include "../utils/Utils.h"
#include "../utils/log.h"

#ifndef INPUTSTREAM_TEST_BUILD
#include <kodi/Filesystem.h>
#include <kodi/General.h>
#endif

#include <algorithm>
#include <chrono>
#include <stdlib.h>
#include <string.h>

using namespace PLAYLIST;
using namespace UTILS;

namespace adaptive
{
  AdaptiveTree::AdaptiveTree(CHOOSER::IRepresentationChooser* reprChooser)
    : m_reprChooser(reprChooser)
  {
  }

  AdaptiveTree::AdaptiveTree(const AdaptiveTree& left) : AdaptiveTree(left.m_reprChooser)
  {
    m_manifestParams = left.m_manifestParams;
    m_manifestHeaders = left.m_manifestHeaders;
    m_settings = left.m_settings;
    m_supportedKeySystem = left.m_supportedKeySystem;
  }

  AdaptiveTree::~AdaptiveTree()
  {
    has_timeshift_buffer_ = false;
    if (updateThread_)
    {
      {
        std::lock_guard<std::mutex> lck(updateMutex_);
        updateVar_.notify_one();
      }
      updateThread_->join();
      delete updateThread_;
    }
  }

  void AdaptiveTree::Configure(const UTILS::PROPERTIES::KodiProperties& kodiProps)
  {
    if (kodi::addon::GetSettingBoolean("debug.save.manifest"))
    {
      m_pathSaveManifest = FILESYS::PathCombine(FILESYS::GetAddonUserPath(), "manifests");
      // Delete previously saved manifest files
      FILESYS::RemoveDirectory(m_pathSaveManifest, false);
    }

    m_manifestParams = kodiProps.m_manifestParams;
    m_manifestHeaders = kodiProps.m_manifestHeaders;

    // Convenience way to share common addon settings we avoid
    // calling the API many times to improve parsing performance
    m_settings.m_bufferAssuredDuration =
        static_cast<uint32_t>(kodi::addon::GetSettingInt("ASSUREDBUFFERDURATION"));
    m_settings.m_bufferMaxDuration =
        static_cast<uint32_t>(kodi::addon::GetSettingInt("MAXBUFFERDURATION"));
  }

  void AdaptiveTree::FreeSegments(CPeriod* period, CRepresentation* repr)
  {
    for (auto& segment : repr->SegmentTimeline().GetData())
    {
      period->DecrasePSSHSetUsageCount(segment.pssh_set_);
    }

    if (repr->HasInitialization() && repr->HasSegmentsUrl())
      repr->initialization_.url.clear();

    repr->SegmentTimeline().Clear();
    repr->current_segment_ = nullptr;
  }

  void AdaptiveTree::SetFragmentDuration(PLAYLIST::CPeriod* period,
                                         PLAYLIST::CAdaptationSet* adpSet,
                                         PLAYLIST::CRepresentation* repr,
                                         size_t pos,
                                         uint64_t timestamp,
                                         uint32_t fragmentDuration,
                                         uint32_t movie_timescale)
  {
    if (!has_timeshift_buffer_ || HasUpdateThread() || repr->HasSegmentsUrl())
      return;

    // Check if its the last frame we watch
    if (!adpSet->SegmentTimelineDuration().IsEmpty())
    {
      if (pos == adpSet->SegmentTimelineDuration().GetSize() - 1)
      {
        adpSet->SegmentTimelineDuration().Insert(
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(fragmentDuration) *
                                       period->GetTimescale() / movie_timescale));
      }
      else
      {
        repr->expired_segments_++;
        return;
      }
    }
    else if (pos != repr->SegmentTimeline().GetSize() - 1)
      return;

    CSegment* segment = repr->SegmentTimeline().Get(pos);

    if (!segment)
    {
      LOG::LogF(LOGERROR, "Segment at position %zu not found from representation id: %s", pos,
                repr->GetId().data());
      return;
    }

    CSegment segCopy = *segment;

    if (!timestamp)
    {
      LOG::LogF(LOGDEBUG, "Scale fragment duration: fdur:%u, rep-scale:%u, mov-scale:%u",
                fragmentDuration, repr->GetTimescale(), movie_timescale);
      fragmentDuration = static_cast<std::uint32_t>(
          (static_cast<std::uint64_t>(fragmentDuration) * repr->GetTimescale()) / movie_timescale);
    }
    else
    {
      LOG::LogF(LOGDEBUG, "Fragment duration from timestamp: ts:%llu, base:%llu, s-pts:%llu",
                timestamp, base_time_, segCopy.startPTS_);
      fragmentDuration = static_cast<uint32_t>(timestamp - base_time_ - segCopy.startPTS_);
    }

    segCopy.startPTS_ += fragmentDuration;
    segCopy.range_begin_ += fragmentDuration;
    segCopy.range_end_++;

    LOG::LogF(LOGDEBUG, "Insert live segment: pts: %llu range_end: %llu", segCopy.startPTS_,
              segCopy.range_end_);

    for (auto& repr : adpSet->GetRepresentations())
    {
      repr->SegmentTimeline().Insert(segCopy);
    }
  }

  void AdaptiveTree::OnDataArrived(uint64_t segNum,
                                   uint16_t psshSet,
                                   uint8_t iv[16],
                                   const uint8_t* src,
                                   std::string& dst,
                                   size_t dstOffset,
                                   size_t dataSize,
                                   bool lastChunk)
  {
    memcpy(&dst[0] + dstOffset, src, dataSize);
  }

  uint16_t AdaptiveTree::InsertPsshSet(PLAYLIST::StreamType streamType,
                                       PLAYLIST::CPeriod* period,
                                       PLAYLIST::CAdaptationSet* adp,
                                       std::string_view pssh,
                                       std::string_view defaultKID,
                                       std::string_view iv /* = "" */)
  {
    if (!pssh.empty())
    {
      CPeriod::PSSHSet psshSet;
      psshSet.pssh_ = pssh;
      psshSet.defaultKID_ = defaultKID;
      psshSet.iv = iv;
      psshSet.m_cryptoMode = m_cryptoMode;
      psshSet.adaptation_set_ = adp;

      if (streamType == StreamType::VIDEO)
        psshSet.media_ = CPeriod::PSSHSet::MEDIA_VIDEO;
      else if (streamType == StreamType::VIDEO_AUDIO)
        psshSet.media_ = CPeriod::PSSHSet::MEDIA_VIDEO | CPeriod::PSSHSet::MEDIA_AUDIO;
      else if (streamType == StreamType::AUDIO)
        psshSet.media_ = CPeriod::PSSHSet::MEDIA_AUDIO;
      else
        psshSet.media_ = CPeriod::PSSHSet::MEDIA_UNSPECIFIED;

      return period->InsertPSSHSet(&psshSet);
    }
    else
      return period->InsertPSSHSet(nullptr);
  }

  bool AdaptiveTree::PreparePaths(const std::string &url)
  {
    if (!URL::IsValidUrl(url))
    {
      LOG::LogF(LOGERROR, "URL not valid (%s)", url.c_str());
      return false;
    }
    manifest_url_ = url;
    base_url_ = URL::RemoveParameters(url);
    return true;
  }

  std::string AdaptiveTree::BuildDownloadUrl(const std::string& url) const
  {
    if (URL::IsUrlAbsolute(url))
      return url;

    return URL::Join(base_url_, url);
  }

  void AdaptiveTree::SortTree()
  {
    for (auto itPeriod = m_periods.begin(); itPeriod != m_periods.end(); itPeriod++)
    {
      CPeriod* period = (*itPeriod).get();
      auto& periodAdpSets = period->GetAdaptationSets();

      // Merge VIDEO & AUDIO adaptation sets
      for (auto itAdpSet = periodAdpSets.begin(); itAdpSet != periodAdpSets.end();)
      {
        auto adpSet = (*itAdpSet).get();
        auto itNextAdpSet = itAdpSet + 1;

        if (itNextAdpSet != periodAdpSets.end() &&
            (adpSet->GetStreamType() == StreamType::AUDIO ||
             adpSet->GetStreamType() == StreamType::VIDEO))
        {
          auto nextAdpSet = (*itNextAdpSet).get();

          if (adpSet->IsMergeable(nextAdpSet))
          {
            std::vector<CPeriod::PSSHSet>& psshSets = period->GetPSSHSets();
            for (size_t index = 1; index < psshSets.size(); index++)
            {
              if (psshSets[index].adaptation_set_ == adpSet)
              {
                psshSets[index].adaptation_set_ = nextAdpSet;
              }
            }
            // Move representations unique_ptr from adpSet repr vector to nextAdpSet repr vector
            std::move(adpSet->GetRepresentations().begin(), adpSet->GetRepresentations().end(),
                      std::inserter(nextAdpSet->GetRepresentations(),
                      nextAdpSet->GetRepresentations().end()));

            itAdpSet = periodAdpSets.erase(itAdpSet);
            continue;
          }
        }
        itAdpSet++;
      }

      std::stable_sort(periodAdpSets.begin(), periodAdpSets.end(),
                       CAdaptationSet::Compare);

      for (auto& adpSet : periodAdpSets)
      {
        std::sort(adpSet->GetRepresentations().begin(), adpSet->GetRepresentations().end(),
                  CRepresentation::CompareBandwidth);

        //! @todo: move set scaling out of SortTree
        for (auto& repr : adpSet->GetRepresentations())
        {
          repr->SetScaling();
        }
      }
    }
  }

  void AdaptiveTree::RefreshUpdateThread()
  {
    if (HasUpdateThread())
    {
      std::lock_guard<std::mutex> lck(updateMutex_);
      updateVar_.notify_one();
    }
  }

  void AdaptiveTree::StartUpdateThread()
  {
    if (!updateThread_ && ~updateInterval_ && has_timeshift_buffer_ && !m_manifestUpdateParam.empty())
      updateThread_ = new std::thread(&AdaptiveTree::SegmentUpdateWorker, this);
  }

  void AdaptiveTree::SegmentUpdateWorker()
  {
    std::unique_lock<std::mutex> updLck(updateMutex_);
    while (~updateInterval_ && has_timeshift_buffer_)
    {
      if (updateVar_.wait_for(updLck, std::chrono::milliseconds(updateInterval_)) == std::cv_status::timeout)
      {
        std::lock_guard<std::mutex> lck(treeMutex_);
        lastUpdated_ = std::chrono::system_clock::now();
        RefreshLiveSegments();
      }
    }
  }

  bool AdaptiveTree::DownloadManifest(std::string url,
                                      const std::map<std::string, std::string>& addHeaders,
                                      std::string& data,
                                      HTTPRespHeaders& respHeaders)
  {
    std::map<std::string, std::string> manifestHeaders = m_manifestHeaders;
    // Merge additional headers to the predefined one
    for (auto& headerIt : addHeaders)
    {
      manifestHeaders[headerIt.first] = headerIt.second;
    }

    // Append manifest parameters, only if not already provided (e.g. manifest update)
    if (url.find('?') == std::string::npos)
      URL::AppendParameters(url, m_manifestParams);

    return download(url, manifestHeaders, data, respHeaders);
  }

  bool AdaptiveTree::download(const std::string& url,
                              const std::map<std::string, std::string>& reqHeaders,
                              std::string& data,
                              HTTPRespHeaders& respHeaders)
  {
    // open the file
    kodi::vfs::CFile file;
    if (!file.CURLCreate(url))
      return false;

    file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "seekable", "0");
    file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "acceptencoding", "gzip");

    for (const auto& entry : reqHeaders)
    {
      file.CURLAddOption(ADDON_CURL_OPTION_HEADER, entry.first.c_str(), entry.second.c_str());
    }

    if (!file.CURLOpen(ADDON_READ_CHUNKED | ADDON_READ_NO_CACHE))
    {
      LOG::Log(LOGERROR, "CURLOpen returned an error, download failed: %s", url.c_str());
      return false;
    }

    respHeaders.m_effectiveUrl = file.GetPropertyValue(ADDON_FILE_PROPERTY_EFFECTIVE_URL, "");

    // Get body lenght (could be gzip compressed)
    std::string contentLengthStr =
        file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, "Content-Length");
    size_t fileSize = static_cast<size_t>(STRING::ToUint64(contentLengthStr));

    static const size_t bufferSize{16 * 1024}; // 16 Kbyte
    std::vector<char> bufferData(bufferSize);
    bool isEOF{false};

    data.reserve(fileSize == 0 ? bufferSize : fileSize);

    // read the file
    while (!isEOF)
    {
      // Read the data in chunks
      ssize_t byteRead{file.Read(bufferData.data(), bufferSize)};
      if (byteRead == -1)
      {
        LOG::Log(LOGERROR, "An error occurred in the download: %s", url.c_str());
        break;
      }
      else if (byteRead == 0) // EOF or undetectable error
      {
        isEOF = true;
      }
      else
      {
        data.append(bufferData.data(), byteRead);
      }
    }

    if (isEOF)
    {
      if (data.size() > 0)
      {
        if (fileSize == 0)
          fileSize = data.size();
        
        double downloadSpeed{file.GetFileDownloadSpeed()};
        // The download speed with small file sizes are not accurate
        // we should have at least 512Kb to have a sufficient acceptable value
        // to calculate the bandwidth, then we make a proportion for cases
        // with less than 512Kb to have a better value
        static const int minSize{512 * 1024};
        if (fileSize < minSize)
          downloadSpeed = (downloadSpeed / fileSize) * minSize;

        respHeaders.m_etag = file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, "etag");
        respHeaders.m_lastModified =
            file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, "last-modified");

        // We set the download speed to calculate the initial network bandwidth
        m_reprChooser->SetDownloadSpeed(downloadSpeed);

        LOG::Log(LOGDEBUG, "Download finished: %s (downloaded %zu byte, speed %0.2lf byte/s)",
                 url.c_str(), fileSize, downloadSpeed);

        file.Close();
        return true;
      }
      else
      {
        LOG::Log(LOGERROR, "A problem occurred in the download, no data received: %s", url.c_str());
      }
    }

    file.Close();
    return false;
  }

  void AdaptiveTree::SaveManifest(const std::string& fileNameSuffix,
                                  std::string_view data,
                                  std::string_view info)
  {
    if (!m_pathSaveManifest.empty())
    {
      // We create a filename based on current timestamp
      // to allow files to be kept in download order useful for live streams
      std::string filename = "manifest_" + std::to_string(UTILS::GetTimestamp());
      if (!fileNameSuffix.empty())
        filename += "_" + fileNameSuffix;

      filename += ".txt";
      std::string filePath = FILESYS::PathCombine(m_pathSaveManifest, filename);

      // Manage duplicate files and limit them, too many means a problem to be solved
      if (FILESYS::CheckDuplicateFilePath(filePath, 10))
      {
        std::string dataToSave = data.data();
        if (!info.empty())
        {
          dataToSave.insert(0, "\n\n");
          dataToSave.insert(0, info);
        }

        if (FILESYS::SaveFile(filePath, dataToSave, false))
          LOG::Log(LOGDEBUG, "Manifest saved to: %s", filePath.c_str());
      }
    }
  }

} // namespace
