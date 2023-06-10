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
#include <kodi/General.h>
#endif

#include <algorithm>
// #include <cassert>
#include <chrono>
#include <stdlib.h>
#include <string.h>

using namespace PLAYLIST;
using namespace UTILS;

namespace adaptive
{
  AdaptiveTree::AdaptiveTree(const AdaptiveTree& left) : AdaptiveTree()
  {
    m_reprChooser = left.m_reprChooser;
    m_manifestParams = left.m_manifestParams;
    m_manifestHeaders = left.m_manifestHeaders;
    m_settings = left.m_settings;
    m_supportedKeySystem = left.m_supportedKeySystem;
  }

  void AdaptiveTree::Configure(const UTILS::PROPERTIES::KodiProperties& kodiProps,
                               CHOOSER::IRepresentationChooser* reprChooser,
                               std::string_view supportedKeySystem,
                               std::string_view manifestUpdateParam)
  {
    m_reprChooser = reprChooser;
    m_supportedKeySystem = supportedKeySystem;

    if (kodi::addon::GetSettingBoolean("debug.save.manifest"))
    {
      m_pathSaveManifest = FILESYS::PathCombine(FILESYS::GetAddonUserPath(), "manifests");
      // Delete previously saved manifest files
      FILESYS::RemoveDirectory(m_pathSaveManifest, false);
    }

    m_manifestParams = kodiProps.m_manifestParams;
    m_manifestHeaders = kodiProps.m_manifestHeaders;
    m_manifestUpdateParam = manifestUpdateParam;

    // Convenience way to share common addon settings we avoid
    // calling the API many times to improve parsing performance
    m_settings.m_bufferAssuredDuration =
        static_cast<uint32_t>(kodi::addon::GetSettingInt("ASSUREDBUFFERDURATION"));
    m_settings.m_bufferMaxDuration =
        static_cast<uint32_t>(kodi::addon::GetSettingInt("MAXBUFFERDURATION"));
  }

  void AdaptiveTree::PostOpen(const UTILS::PROPERTIES::KodiProperties& kodiProps)
  {
    FixStreamsFlags(kodiProps);
    
    // A manifest can provide live delay value, if not so we use our default
    // value of 16 secs, this is needed to ensure an appropriate playback,
    // an add-on can override the delay to try fix edge use cases
    if (kodiProps.m_liveDelay >= 16)
      m_liveDelay = kodiProps.m_liveDelay;
    else if (m_liveDelay < 16)
      m_liveDelay = 16;

    StartUpdateThread();

    LOG::Log(LOGINFO,
             "Manifest successfully parsed (Periods: %zu, Streams in first period: %zu, Type: %s)",
             m_periods.size(), m_currentPeriod->GetAdaptationSets().size(),
             has_timeshift_buffer_ ? "live" : "VOD");
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
    if (!has_timeshift_buffer_ || HasManifestUpdates() || repr->HasSegmentsUrl())
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
                                   const char* srcData,
                                   size_t srcDataSize,
                                   std::string& segBuffer,
                                   size_t segBufferSize,
                                   bool isLastChunk)
  {
    segBuffer.append(srcData, srcDataSize);
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
      //! @todo: seem that merge adpsets is this not safe thing to do, adpsets may have different encryptions
      //!        and relative different child data (e.g. dash xml child tags)
      //!        it is needed to investigate if we really need do this,
      //!        if so maybe limit for some use cases or improve it in some way.
      //!        audio merging has been impl years ago without give any details of the reasons or for what manifest types
      //!        video merging has been impl by https://github.com/xbmc/inputstream.adaptive/pull/694
      //!        second thing, merge should be decoupled from sort behaviour with different methods
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
            for (auto itRepr = adpSet->GetRepresentations().begin();
                 itRepr != adpSet->GetRepresentations().end(); itRepr++)
            {
              nextAdpSet->GetRepresentations().push_back(std::move(*itRepr));
              // We need to change the parent adaptation set in the representation itself
              nextAdpSet->GetRepresentations().back()->SetParent(nextAdpSet);
            }

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
      }
    }
  }

  void AdaptiveTree::StartUpdateThread()
  {
    if (HasManifestUpdates())
      m_updThread.Initialize(this);
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
    m_threadStop = true;

    if (m_thread.joinable())
    {
      m_cvUpdInterval.notify_all(); // Unlock possible waiting
      m_thread.join();
    }
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

    while (~m_tree->m_updateInterval && !m_threadStop)
    {
      if (m_cvUpdInterval.wait_for(updLck, std::chrono::milliseconds(m_tree->m_updateInterval)) ==
          std::cv_status::timeout)
      {
        updLck.unlock();
        // If paused, wait until last "Resume" will be called
        std::unique_lock<std::mutex> lckWait(m_waitMutex);
        m_cvWait.wait(lckWait, [&] { return m_waitQueue == 0; });

        updLck.lock();
        m_tree->RefreshLiveSegments();
      }
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

  void AdaptiveTree::FixStreamsFlags(const UTILS::PROPERTIES::KodiProperties& kodiProps)
  {
    // Add-ons can override subtitles "default" flag to streams
    if (!kodiProps.m_subtitleLangDefault.empty())
    {
      for (auto& period : m_periods)
      {
        for (auto& adpSet : period->GetAdaptationSets())
        {
          if (adpSet->GetStreamType() == StreamType::SUBTITLE)
          {
            adpSet->SetIsDefault(
                STRING::CompareNoCase(adpSet->GetLanguage(), kodiProps.m_subtitleLangDefault));
          }
        }
      }
    }

    // Add-ons can override audio "original" flag to streams
    if (!kodiProps.m_audioLangOriginal.empty())
    {
      for (auto& period : m_periods)
      {
        for (auto& adpSet : period->GetAdaptationSets())
        {
          if (adpSet->GetStreamType() == StreamType::AUDIO)
          {
            adpSet->SetIsOriginal(
                STRING::CompareNoCase(adpSet->GetLanguage(), kodiProps.m_audioLangOriginal));
          }
        }
      }
    }

    // Audio streams "default" flag customization / workaround

    // Manifest of video services dont always set appropriately the default stream flag and also
    // the manifest "default" stream flag dont have always the same meaning of Kodi track "default" flag,
    // this can lead to wrong audio track selected when playback start.
    // A good example is when "Media default" Kodi audio setting is set, where kodi expects just
    // a single track with the default flag.
    // Another problem is when video services provide multiple audio streams with same language code
    // but differents channels, most of the times we can have 1 stereo and 1 multichannels
    // stream with same language code, rarely there are multi-codecs with same channels,
    // but we simplify by ignoring codec types.

    // To allow Kodi VP to do a better track auto-selection we need:
    // - Set default flag to a single track only
    // - Set default flag to stereo or multichannels track, not both
    // to do this its needed that an addon specify what to do because C++ interface dont provide
    // access to kodi language settings where python can do it, then we cant automatize it.
    const std::string langCodeDef = kodiProps.m_audioLangDefault;
    const std::string langCodeOrig = kodiProps.m_audioLangOriginal;

    if (!langCodeDef.empty() || !langCodeOrig.empty())
    {
      bool isDefaultStereo = kodiProps.m_audioPrefStereo; // add-on based setting

      for (auto& period : m_periods)
      {
        auto& adpSets = period->GetAdaptationSets();
        auto itAudioStream = adpSets.cend();

        // Try give priority to "impaired" streams
        if (kodiProps.m_audioPrefType == "impaired")
        {
          if (isDefaultStereo)
          {
            itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeDef, true, true);
            if (itAudioStream == adpSets.cend()) // No stereo stream, find multichannels
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeDef, false, true);
          }
          else
          {
            itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeDef, false, true);
            if (itAudioStream == adpSets.cend()) // No multichannels stream, find stereo
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeDef, true, true);
          }

          // No stream found, try find a "impaired" stream with the "original" language code
          if (itAudioStream == adpSets.cend() && !langCodeOrig.empty())
          {
            if (isDefaultStereo)
            {
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeOrig, true, true);
              if (itAudioStream == adpSets.cend()) // No stereo stream, find multichannels
                itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeOrig, false, true);
            }
            else
            {
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeOrig, false, true);
              if (itAudioStream == adpSets.cend()) // No multichannels stream, find stereo
                itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeOrig, true, true);
            }
          }
        }

        // Try find a stream with specified lang code
        if (kodiProps.m_audioPrefType != "original" && itAudioStream == adpSets.cend() &&
            !langCodeDef.empty())
        {
          if (isDefaultStereo)
          {
            itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeDef, true);
            if (itAudioStream == adpSets.cend()) // No stereo stream, find multichannels
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeDef, false);
          }
          else
          {
            itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeDef, false);
            if (itAudioStream == adpSets.cend()) // No multichannels stream, find stereo
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeDef, true);
          }
        }

        // No stream found, try find a stream with the "original" language code
        if (itAudioStream == adpSets.cend() && !langCodeOrig.empty())
        {
          if (isDefaultStereo)
          {
            itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeOrig, true);
            if (itAudioStream == adpSets.cend()) // No stereo stream, find multichannels
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeOrig, false);
          }
          else
          {
            itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeOrig, false);
            if (itAudioStream == adpSets.cend()) // No multichannels stream, find stereo
              itAudioStream = CAdaptationSet::FindAudioAdpSet(adpSets, langCodeOrig, true);
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
  }

  } // namespace adaptive
