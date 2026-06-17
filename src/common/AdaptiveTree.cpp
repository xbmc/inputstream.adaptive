/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AdaptiveTree.h"

#include "Chooser.h"
#include "CompKodiProps.h"
#include "CompSettings.h"
#include "SrvBroker.h"
#include "common/AdaptiveUtils.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <algorithm>
// #include <cassert>
#include <chrono>

using namespace PLAYLIST;
using namespace UTILS;

namespace adaptive
{
  AdaptiveTree::AdaptiveTree(const AdaptiveTree& left) : AdaptiveTree()
  {
    m_reprChooser = left.m_reprChooser;
    m_manifestParams = left.m_manifestParams;
    m_manifestHeaders = left.m_manifestHeaders;
    m_pathSaveManifest = left.m_pathSaveManifest;
    stream_start_ = left.stream_start_;

    m_isTTMLTimeRelative = left.m_isTTMLTimeRelative;
    m_isReqPrepareStream = left.m_isReqPrepareStream;
  }

  void AdaptiveTree::Configure(CHOOSER::IRepresentationChooser* reprChooser,
                               const std::string& manifestUpdParams)
  {
    m_reprChooser = reprChooser;

    auto srvBroker = CSrvBroker::GetInstance();

    if (srvBroker->GetSettings().IsDebugManifest())
    {
      m_pathSaveManifest = FILESYS::PathCombine(FILESYS::GetAddonUserPath(), "manifests");
      // Delete previously saved manifest files
      FILESYS::RemoveDirectory(m_pathSaveManifest, false);
    }

    m_manifestParams = srvBroker->GetKodiProps().GetManifestParams();
    m_manifestHeaders = srvBroker->GetKodiProps().GetManifestHeaders();
    m_manifestUpdParams = manifestUpdParams;
    stream_start_ = GetTimestamp();
  }

  uint64_t AdaptiveTree::GetTimestamp()
  {
    return UTILS::GetTimestampMs();
  }

  void AdaptiveTree::Uninitialize()
  {
    // Stop the update thread before the tree class deconstruction otherwise derived classes
    // will be destructed early, so while an update could be just started
    m_updThread.Stop();
  }

  void AdaptiveTree::PostOpen()
  {
    SortTree();
    OverrideStreamsMediaFlags(m_periods);

    // A manifest can provide live delay value, if not so we use our default
    // value of 16 secs, this is needed to ensure an appropriate playback,
    // an add-on can override the delay to try fix edge use cases
    uint64_t liveDelay = CSrvBroker::GetKodiProps().GetManifestConfig().liveDelay;
    if (liveDelay >= 16)
      m_liveDelay = liveDelay;
    else if (m_liveDelay < 16)
      m_liveDelay = 16;

    StartUpdateThread();

    LOG::Log(LOGINFO,
             "Manifest successfully parsed (Periods: %zu, Streams in first period: %zu, Type: %s)",
             m_periods.size(), m_currentPeriod->GetAdaptationSets().size(),
             m_isLive ? "live" : "VOD");
  }

  void AdaptiveTree::OnDataArrived(uint64_t segNum,
                                   std::optional<CAesKeyInfo>& aesKey,
                                   uint8_t iv[16],
                                   const uint8_t* srcData,
                                   size_t srcDataSize,
                                   std::vector<uint8_t>& segBuffer,
                                   size_t segBufferSize,
                                   bool isLastChunk)
  {
    segBuffer.insert(segBuffer.end(), srcData, srcData + srcDataSize);
  }

  void AdaptiveTree::OverrideStreamsMediaFlags(
      std::vector<std::unique_ptr<PLAYLIST::CPeriod>>& periods)
  {
    const auto& config = CSrvBroker::GetKodiProps().GetConfig();

    const std::string& audioLangCodeDef = config.mediaAudioLangCodeDef;
    const std::string& audioLangCodeOrig = config.mediaAudioLangCodeOrig;
    const std::string& subtitleLangCodeDef = config.mediaSubtitleLangCodeDef;
    const bool isAudioStereoPref = config.mediaAudioStereoPref;
    const auto audioTypePref = config.mediaAudioTypePref;

    // Media flags note:
    // Manifests of video services dont always set appropriately the default stream media flags
    // moreover the manifest "default" stream flag dont have always the same meaning of Kodi track "default" flag
    // so this can lead to wrong audio track selected when playback start.
    //
    // NOTE: to simplify the code it dont take in account of multi-codecs streams with same language code and channels,
    //  this is not a common use case and we ignore it.
    // NOTE 2: To allow Kodi VP to do a better track auto-selection we need:
    //  - Set default/impaired/original flag to a single track only
    //  - Set default/impaired/original flag to stereo or multichannels track, not both
    // NOTE 3: At moment Kodi dont support any kind of track auto-selection fallback for language code
    //  that have variants e.g. "pt-BR" for Brazilian Portuguese vs "pt" for Portuguese,
    //  so here at moment its the same, if you set as default "pt-BR" but exists only a "pt" stream
    //  no stream will be set as default. This could be improved in future but for now we keep it simple.
    //
    //! @todo: For the way it works, we have to rely on each add-on to provide the necessary parameters.
    //! This is because the C++ interface does not provide access to language settings,
    //! whereas the Python interface has access to everything. This could be improved in the future.

    // Override subtitles "default" flag to streams
    if (!audioLangCodeDef.empty())
    {
      for (auto& period : periods)
      {
        for (auto& adpSet : period->GetAdaptationSets())
        {
          if (adpSet->GetStreamType() == StreamType::SUBTITLE)
          {
            adpSet->SetIsDefault(STRING::CompareNoCase(adpSet->GetLanguage(), audioLangCodeDef));
          }
        }
      }
    }

    // Override audio "original" flag to streams
    if (!audioLangCodeOrig.empty())
    {
      for (auto& period : periods)
      {
        auto& adpSets = period->GetAdaptationSets();
        auto itAudioStream = adpSets.cend();

        if (isAudioStereoPref)
        {
          itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeOrig, true);
          if (itAudioStream == adpSets.cend()) // No stereo stream, find multichannels
            itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeOrig, false);
        }
        else
        {
          itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeOrig, false);
          if (itAudioStream == adpSets.cend()) // No multichannels stream, find stereo
            itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeOrig, true);
        }

        // Update "original" flags
        if (itAudioStream != adpSets.cend())
        {
          for (auto& adpSet : adpSets)
          {
            adpSet->SetIsOriginal(adpSet.get() == itAudioStream->get());
          }
        }
      }
    }

    // Override audio "default" flag to streams, we give priority to "impaired" streams if specified
    if (!audioLangCodeDef.empty() || !audioLangCodeOrig.empty())
    {
      for (auto& period : periods)
      {
        auto& adpSets = period->GetAdaptationSets();
        auto itAudioStream = adpSets.cend();

        // Try give priority to "impaired" streams
        if (audioTypePref == ADP::KODI_PROPS::MediaFlagType::IMPAIRED)
        {
          if (isAudioStereoPref)
          {
            itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeDef, true, true);
            if (itAudioStream == adpSets.cend()) // No stereo stream, find multichannels
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeDef, false, true);
          }
          else
          {
            itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeDef, false, true);
            if (itAudioStream == adpSets.cend()) // No multichannels stream, find stereo
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeDef, true, true);
          }

          // No stream found, try find a "impaired" stream with the "original" language code
          if (itAudioStream == adpSets.cend() && !audioLangCodeOrig.empty())
          {
            if (isAudioStereoPref)
            {
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeOrig, true, true);
              if (itAudioStream == adpSets.cend()) // No stereo stream, find multichannels
                itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeOrig, false, true);
            }
            else
            {
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeOrig, false, true);
              if (itAudioStream == adpSets.cend()) // No multichannels stream, find stereo
                itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeOrig, true, true);
            }
          }
        }

        // Try find a stream with specified lang code
        if (audioTypePref != ADP::KODI_PROPS::MediaFlagType::ORIGINAL &&
            itAudioStream == adpSets.cend() && !audioLangCodeDef.empty())
        {
          if (isAudioStereoPref)
          {
            itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeDef, true);
            if (itAudioStream == adpSets.cend()) // No stereo stream, find multichannels
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeDef, false);
          }
          else
          {
            itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeDef, false);
            if (itAudioStream == adpSets.cend()) // No multichannels stream, find stereo
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeDef, true);
          }
        }

        // No stream found, try find a stream with the "original" language code
        if (itAudioStream == adpSets.cend() && !audioLangCodeOrig.empty())
        {
          if (isAudioStereoPref)
          {
            itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeOrig, true);
            if (itAudioStream == adpSets.cend()) // No stereo stream, find multichannels
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeOrig, false);
          }
          else
          {
            itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeOrig, false);
            if (itAudioStream == adpSets.cend()) // No multichannels stream, find stereo
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, audioLangCodeOrig, true);
          }
        }

        // Update "default" flags
        if (itAudioStream != adpSets.cend())
        {
          for (auto& adpSet : adpSets)
          {
            adpSet->SetIsDefault(adpSet.get() == itAudioStream->get());
          }
        }
      }
    }

    // Override subtitles "default" flag to streams
    if (!subtitleLangCodeDef.empty())
    {
      for (auto& period : periods)
      {
        auto& adpSets = period->GetAdaptationSets();

        auto itSubtitleStream = CAdaptationSet::FindSubtitleAdpSet(adpSets, subtitleLangCodeDef, false);

        // Update "default" flags
        if (itSubtitleStream != adpSets.cend())
        {
          for (auto& adpSet : adpSets)
          {
            adpSet->SetIsDefault(adpSet.get() == itSubtitleStream->get());
          }
        }
      }
    }
  }

  void AdaptiveTree::SortTree()
  {
    for (auto& period : m_periods)
    {
      auto& adpSets = period->GetAdaptationSets();

      std::stable_sort(adpSets.begin(), adpSets.end(), CAdaptationSet::Compare);

      for (auto& adpSet : adpSets)
      {
        std::sort(adpSet->GetRepresentations().begin(), adpSet->GetRepresentations().end(),
                  CRepresentation::CompareBandwidth);
      }
    }
  }

  void AdaptiveTree::StartUpdateThread()
  {
    if (HasManifestUpdates())
      m_updThread.Initialize(this);
  }

  bool AdaptiveTree::IsLastSegment(const PLAYLIST::CPeriod* segPeriod,
                                   const PLAYLIST::CRepresentation* segRep,
                                   std::optional<PLAYLIST::CSegment> segment) const
  {
    if (segRep->Timeline().IsEmpty())
      return true;

    if (!segment.has_value() || segment->startPTS_ == NO_PTS_VALUE ||
        segment->m_endPts == NO_PTS_VALUE || !segPeriod || !segRep)
      return false;

    if (IsLive())
    {
      if (segPeriod->GetDuration() > 0 && segPeriod->GetStart() != NO_VALUE)
      {
        const uint64_t pDurMs = segPeriod->GetDuration() * 1000 / segPeriod->GetTimescale();
        const uint64_t pEndPtsMs = segPeriod->GetStart() + pDurMs;

        const uint64_t segEndPtsMs = segment->m_endPts * 1000 / segRep->GetTimescale();

        LOG::LogF(LOGDEBUG, "Check for last segment (period end PTS: %llu, segment end PTS: %llu)",
                  pEndPtsMs, segEndPtsMs);

        return segEndPtsMs >= pEndPtsMs;
      }
    }
    else
    {
      const CSegment& lastSeg = *segRep->Timeline().GetBack();
      return segment->IsSame(lastSeg);
    }
    return false;
  }

  void AdaptiveTree::SaveManifest(const std::string& fileNameSuffix,
                                  const std::string& data,
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
        std::string dataToSave = data;
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

  AdaptiveTree::TreeUpdateThread::~TreeUpdateThread()
  {
    // assert(m_waitQueue == 0); // Debug only, missing resume

    // We assume that Stop() method has been called before the deconstruction
    m_threadStop = true;

    if (m_thread.joinable())
      m_thread.join();
  }

  void AdaptiveTree::TreeUpdateThread::Initialize(AdaptiveTree* tree)
  {
    if (!m_thread.joinable())
    {
      m_tree = tree;
      m_thread = std::thread(&TreeUpdateThread::Worker, this);
    }
  }

  void AdaptiveTree::TreeUpdateThread::Worker()
  {
    std::unique_lock<std::mutex> updLck(m_updMutex);

    while (m_tree->m_updateInterval != NO_VALUE && m_tree->m_updateInterval > 0 && !m_threadStop)
    {
      auto nowTime = std::chrono::steady_clock::now();

      std::chrono::milliseconds intervalMs = std::chrono::milliseconds(m_tree->m_updateInterval);
      // Wait for the interval time, the predicate method is used to avoid spurious wakeups
      // and to allow exit early when notify_all is called to force stop operations
      m_cvUpdInterval.wait_for(updLck, intervalMs,
                               [&nowTime, &intervalMs, this] {
                                 return std::chrono::steady_clock::now() - nowTime >= intervalMs ||
                                        m_threadStop;
                               });

      updLck.unlock();
      // If paused, wait until last "Resume" will be called
      std::unique_lock<std::mutex> lckWait(m_waitMutex);
      m_cvWait.wait(lckWait, [&] { return m_waitQueue == 0; });
      if (m_threadStop)
        break;

      updLck.lock();

      // Reset interval value to allow forced update from manifest
      if (m_resetInterval)
        m_tree->m_updateInterval = PLAYLIST::NO_VALUE;

      m_tree->OnUpdateSegments();
    }
  }

  void AdaptiveTree::TreeUpdateThread::Pause()
  {
    // If an update is already in progress the wait until its finished
    std::lock_guard<std::mutex> updLck{m_updMutex};
    m_waitQueue++;
  }

  void AdaptiveTree::TreeUpdateThread::Resume()
  {
    // assert(m_waitQueue != 0); // Debug only, resume without any pause
    m_waitQueue--;
    // If there are no more pauses, unblock the update thread
    if (m_waitQueue == 0)
      m_cvWait.notify_all();
  }

  void AdaptiveTree::TreeUpdateThread::Stop()
  {
    m_threadStop = true;
    // If an update is already in progress wait until exit
    std::lock_guard<std::mutex> updLck{m_updMutex};
    m_cvUpdInterval.notify_all();
    m_cvWait.notify_all();
  }

  } // namespace adaptive
