/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../utils/CryptoUtils.h"
#include "../utils/PropertiesUtils.h"
#include "AdaptationSet.h"
#include "Period.h"
#include "Representation.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef INPUTSTREAM_TEST_BUILD
#include "../test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

// Forward namespace/class
namespace CHOOSER
{
class IRepresentationChooser;
}

namespace adaptive
{

class ATTR_DLL_LOCAL AdaptiveTree
{
public:
  struct Settings
  {
    uint32_t m_bufferAssuredDuration{60};
    uint32_t m_bufferMaxDuration{120};
  };

  std::vector<std::unique_ptr<PLAYLIST::CPeriod>> m_periods;
  PLAYLIST::CPeriod* m_currentPeriod{nullptr};
  PLAYLIST::CPeriod* m_nextPeriod{nullptr};
  PLAYLIST::CAdaptationSet* m_currentAdpSet{nullptr};
  PLAYLIST::CRepresentation* m_currentRepr{nullptr};

  std::string manifest_url_;
  std::string base_url_;
  
  //! @todo: m_manifestUpdateParam is used to force enabling manifest updates and to
  //!  be able to set also parameters to the manifest update url, we should start decouple
  //!  the "full" use case and the parameters case with appropriate separate properties,
  //!  in a future "full" use case should be dropped and possible broken dash live manifest fixed
  std::string m_manifestUpdateParam;

  std::optional<uint32_t> initial_sequence_; // HLS only
  uint64_t m_totalTimeSecs{0}; // Total playing time in seconds
  uint64_t stream_start_{0};
  uint64_t available_time_{0};
  uint64_t base_time_{0}; // SmoothTree only, the lower start PTS time between all StreamIndex tags
  uint64_t m_liveDelay{0}; // Apply a delay in seconds from the live edge
  bool has_timeshift_buffer_{false}; // Returns true when there is timeshift buffer for live content
  
  std::string m_supportedKeySystem;
  std::string location_;

  CryptoMode m_cryptoMode{CryptoMode::NONE};

  AdaptiveTree() = default;
  AdaptiveTree(const AdaptiveTree& left);
  virtual ~AdaptiveTree() = default;

  /*!
   * \brief Configure the adaptive tree.
   * \param kodiProps The Kodi properties
   * \param manifestUpdateParam Set to "full" to force enabling future manifest updates or set parameters
   *                            that will be add to manifest request url, with an optional support
   *                            of placeholder $START_NUMBER$ to allow set the segment start number
   *                            to the parameter e.g. ?start_seq=$START_NUMBER$ become ?start_seq=10
   */
  virtual void Configure(const UTILS::PROPERTIES::KodiProperties& kodiProps,
                         CHOOSER::IRepresentationChooser* reprChooser,
                         std::string_view supportedKeySystem,
                         std::string_view manifestUpdateParam);

  /*!
   * \brief Open manifest data for parsing.
   * \param url Effective url where the manifest is downloaded
   * \param headers Headers provided in the HTTP response
   * \param data The manifest data
   */
  virtual bool Open(std::string_view url,
                    const std::map<std::string, std::string>& headers,
                    const std::string& data) = 0;

  /*!
   * \brief Performs tasks after opening the manifest
   * \param kodiProps The Kodi properties
   */
  virtual void PostOpen(const UTILS::PROPERTIES::KodiProperties& kodiProps);

  virtual PLAYLIST::PrepareRepStatus prepareRepresentation(PLAYLIST::CPeriod* period,
                                                           PLAYLIST::CAdaptationSet* adp,
                                                           PLAYLIST::CRepresentation* rep,
                                                           bool update = false)
  {
    return PLAYLIST::PrepareRepStatus::OK;
  }

  virtual std::chrono::time_point<std::chrono::system_clock> GetRepLastUpdated(
      const PLAYLIST::CRepresentation* rep)
  {
    return std::chrono::system_clock::now();
  }

  virtual void OnDataArrived(uint64_t segNum,
                             uint16_t psshSet,
                             uint8_t iv[16],
                             const char* srcData,
                             size_t srcDataSize,
                             std::string& segBuffer,
                             size_t segBufferSize,
                             bool isLastChunk);

  virtual void RefreshSegments(PLAYLIST::CPeriod* period,
                               PLAYLIST::CAdaptationSet* adp,
                               PLAYLIST::CRepresentation* rep,
                               PLAYLIST::StreamType type)
  {
  }

  void FreeSegments(PLAYLIST::CPeriod* period, PLAYLIST::CRepresentation* repr);

  void SetFragmentDuration(PLAYLIST::CPeriod* period,
                           PLAYLIST::CAdaptationSet* adpSet,
                           PLAYLIST::CRepresentation* repr,
                           size_t pos,
                           uint64_t timestamp,
                           uint32_t fragmentDuration,
                           uint32_t movie_timescale);

  // Insert a PSSHSet to the specified Period and return the position
  uint16_t InsertPsshSet(PLAYLIST::StreamType streamType,
                         PLAYLIST::CPeriod* period,
                         PLAYLIST::CAdaptationSet* adp,
                         std::string_view pssh,
                         std::string_view defaultKID,
                         std::string_view iv = "");

  PLAYLIST::CAdaptationSet* GetAdaptationSet(size_t pos) const
  {
    return m_currentPeriod && pos < m_currentPeriod->GetAdaptationSets().size()
               ? m_currentPeriod->GetAdaptationSets()[pos].get()
               : nullptr;
  }

  std::string BuildDownloadUrl(const std::string& url) const;

  bool HasManifestUpdates() const
  {
    return ~m_updateInterval && m_updateInterval > 0 && has_timeshift_buffer_ &&
           !m_manifestUpdateParam.empty();
  }

  const std::chrono::time_point<std::chrono::system_clock> GetLastUpdated() const { return lastUpdated_; };

  CHOOSER::IRepresentationChooser* GetRepChooser() { return m_reprChooser; }

  int SecondsSinceRepUpdate(PLAYLIST::CRepresentation* rep)
  {
    return static_cast<int>(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - GetRepLastUpdated(rep))
      .count());
  }

  virtual AdaptiveTree* Clone() const = 0;

  Settings m_settings;

  class TreeUpdateThread
  {
  public:
    TreeUpdateThread() = default;
    ~TreeUpdateThread();

    void Initialize(AdaptiveTree* tree);

    // \brief Reset start time (make exit the condition variable m_cvUpdInterval and re-start the timeout)
    void ResetStartTime() { m_cvUpdInterval.notify_all(); }

    // \brief As "std::mutex" lock, but put in pause the manifest updates (support std::lock_guard).
    //        If an update is in progress, block the code until the update is finished.
    void lock() { Pause(); }

    // \brief As "std::mutex" unlock, but resume the manifest updates (support std::lock_guard).
    void unlock() { Resume(); }

  private:
    void Worker();
    void Pause();
    void Resume();

    std::thread m_thread;
    AdaptiveTree* m_tree{nullptr};

    // Reentrant mode to pause updates, which will be resumed as soon as the queue is removed,
    // each time Pause() is called will add a wait queue
    // each time Resume() is called remove a wait queue
    // when there are no more wait queue, the updates will be resumed.
    std::atomic<uint32_t> m_waitQueue{0};

    std::mutex m_updMutex;
    std::condition_variable m_cvUpdInterval;
    std::mutex m_waitMutex;
    std::condition_variable m_cvWait;
    bool m_threadStop{false};
  };

  /*!
   * \brief Get the tree manifest update thread to be used like an "std::mutex"
   *        to put in pause/resume tree manifest updates betweeen other operations.
   *        NOTE: this is a custom mutex that act as reentrant way,
   *        then can be used at same time by different threads, the code that call the mutex lock
   *        will be blocked only when the manifest updates are already in progress.
   */
  TreeUpdateThread& GetTreeUpdMutex() { return m_updThread; };

  /*!
   * \brief Get the license URL, some DRM-encrypted manifests (e.g. SmoothStreaming) can provide it.
   * \return The license URL if found, otherwise empty string.
   */
  std::string_view GetLicenseUrl() { return m_licenseUrl; }

protected:
  /*!
   * \brief Save manifest data to a file for debugging purpose.
   * \param fileNameSuffix Suffix to add to the filename generated.
   * \param data The manifest data to save.
   * \param info Additionals info to be add before the data.
   */
  virtual void SaveManifest(const std::string& fileNameSuffix,
                            const std::string& data,
                            std::string_view info);

  void SortTree();

  // Live segment update section
  virtual void StartUpdateThread();
  virtual void RefreshLiveSegments() { lastUpdated_ = std::chrono::system_clock::now(); }
  std::atomic<uint32_t> m_updateInterval{~0U};
  TreeUpdateThread m_updThread;
  std::atomic<std::chrono::time_point<std::chrono::system_clock>> lastUpdated_{std::chrono::system_clock::now()};

  std::string m_manifestParams;
  std::map<std::string, std::string> m_manifestHeaders;
  CHOOSER::IRepresentationChooser* m_reprChooser{nullptr};

  // Provide the path where the manifests will be saved, if debug enabled
  std::string m_pathSaveManifest;

  std::string m_licenseUrl;
};

} // namespace adaptive
