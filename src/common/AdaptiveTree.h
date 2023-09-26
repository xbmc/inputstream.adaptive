/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "utils/CryptoUtils.h"
#include "utils/PropertiesUtils.h"
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
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

// Forward namespace/class
namespace CHOOSER
{
class IRepresentationChooser;
}
namespace PLAYLIST
{
enum class TreeType;
}

namespace adaptive
{

// \brief Adaptive tree types
enum class TreeType
{
  UNKNOWN,
  DASH,
  HLS,
  SMOOTH_STREAMING
};

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
  
  std::optional<uint32_t> initial_sequence_; // HLS only
  uint64_t m_totalTimeSecs{0}; // Total playing time in seconds
  uint64_t stream_start_{0};
  uint64_t available_time_{0};
  uint64_t m_liveDelay{0}; // Apply a delay in seconds from the live edge
  
  std::string m_supportedKeySystem;
  std::string location_;

  CryptoMode m_cryptoMode{CryptoMode::NONE};

  AdaptiveTree() = default;
  AdaptiveTree(const AdaptiveTree& left);
  virtual ~AdaptiveTree() = default;

  virtual TreeType GetTreeType() { return TreeType::UNKNOWN; }

  /*!
   * \brief Configure the adaptive tree.
   * \param kodiProps The Kodi properties
   * \param manifestUpdParams Parameters to be add to manifest request url, depends on manifest implementation
   */
  virtual void Configure(const UTILS::PROPERTIES::KodiProperties& kodiProps,
                         CHOOSER::IRepresentationChooser* reprChooser,
                         std::string_view supportedKeySystem,
                         std::string_view manifestUpdParams);

  /*!
   * \brief Performs operations to stop running process and release resources.
   */
  virtual void Uninitialize();

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
                             const uint8_t* srcData,
                             size_t srcDataSize,
                             std::vector<uint8_t>& segBuffer,
                             size_t segBufferSize,
                             bool isLastChunk);

  virtual void RefreshSegments(PLAYLIST::CPeriod* period,
                               PLAYLIST::CAdaptationSet* adp,
                               PLAYLIST::CRepresentation* rep,
                               PLAYLIST::StreamType type)
  {
  }

  void FreeSegments(PLAYLIST::CPeriod* period, PLAYLIST::CRepresentation* repr);

  /*!
   * \brief Some adaptive streaming protocols allow the client to download the live playlist once and
   *        build future segments based on metadata contained in the fragments e.g. to avoid repeated
   *        manifest downloads or to cover the duration of a period not fully covered by the provided timeline.
   * \param period Current period
   * \param adpSet Current adaptation set
   * \param repr Current representation
   * \param pos Current segment position
   * \param timestamp Fragment start timestamp
   * \param fragmentDuration Fragment duration
   * \param movieTimescale Fragment movie timescale
   */
  virtual void InsertLiveSegment(PLAYLIST::CPeriod* period,
                                 PLAYLIST::CAdaptationSet* adpSet,
                                 PLAYLIST::CRepresentation* repr,
                                 size_t pos,
                                 uint64_t timestamp,
                                 uint64_t fragmentDuration,
                                 uint32_t movieTimescale)
  {
  }

  uint16_t InsertPsshSet(PLAYLIST::StreamType streamType,
                         PLAYLIST::CPeriod* period,
                         PLAYLIST::CAdaptationSet* adp,
                         const std::vector<uint8_t>& pssh,
                         const std::vector<uint8_t>& defaultKid,
                         bool needsExtractPssh = false);

  uint16_t InsertPsshSet(PLAYLIST::StreamType streamType,
                         PLAYLIST::CPeriod* period,
                         PLAYLIST::CAdaptationSet* adp,
                         const std::vector<uint8_t>& pssh,
                         const std::vector<uint8_t>& defaultKid,
                         std::string_view kidUrl,
                         std::string_view iv);

  PLAYLIST::CAdaptationSet* GetAdaptationSet(size_t pos) const
  {
    return m_currentPeriod && pos < m_currentPeriod->GetAdaptationSets().size()
               ? m_currentPeriod->GetAdaptationSets()[pos].get()
               : nullptr;
  }

  /*!
   * \brief Check for live streaming content (timeshift buffer)
   * \return True for live streaming content, otherwise false for VOD content
   */
  bool IsLive() const { return m_isLive; }

  /*!
   * \brief Determines if a live manifest needs updates when new segments are requested
   */
  bool HasManifestUpdatesSegs() const { return m_isLive && m_updateInterval == 0; }

  /*!
   * \brief Determines if a live manifest needs updates that are handled automatically
   *        by using time intervals
   */
  bool HasManifestUpdates() const
  {
    return m_isLive && m_updateInterval != PLAYLIST::NO_VALUE && m_updateInterval > 0;
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

    // \brief Stop performing new updates.
    void Stop();

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
  // Insert a PSSHSet to the specified Period and return the position
  uint16_t InsertPsshSetImpl(PLAYLIST::StreamType streamType,
                             PLAYLIST::CPeriod* period,
                             PLAYLIST::CAdaptationSet* adp,
                             const std::vector<uint8_t>& pssh,
                             const std::vector<uint8_t>& defaultKID,
                             std::string_view kidUrl,
                             std::string_view iv,
                             bool needsExtractPssh);

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
  bool m_isLive{false};
  virtual void StartUpdateThread();
  virtual void RefreshLiveSegments() { lastUpdated_ = std::chrono::system_clock::now(); }

  // Manifest update interval in ms,
  // Non-zero value: refresh interval starting from the moment mpd download was initiated
  // Value 0: refresh each time we need to make new segments
  std::atomic<uint64_t> m_updateInterval{PLAYLIST::NO_VALUE};
  TreeUpdateThread m_updThread;
  std::atomic<std::chrono::time_point<std::chrono::system_clock>> lastUpdated_{std::chrono::system_clock::now()};

  // Optionals URL parameters to add to the manifest update requests
  std::string m_manifestUpdParams;
  std::string m_manifestParams;
  std::map<std::string, std::string> m_manifestHeaders;
  CHOOSER::IRepresentationChooser* m_reprChooser{nullptr};

  // Provide the path where the manifests will be saved, if debug enabled
  std::string m_pathSaveManifest;

  std::string m_licenseUrl;
};

} // namespace adaptive
