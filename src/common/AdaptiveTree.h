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

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
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

struct ATTR_DLL_LOCAL HTTPRespHeaders {

  std::string m_effectiveUrl;
  std::string m_etag; // etag header
  std::string m_lastModified; // last-modified header
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
  std::string effective_url_;
  std::string m_manifestUpdateParam;

  std::optional<uint32_t> initial_sequence_; // HLS only
  uint64_t m_totalTimeSecs{0}; // Total playing time in seconds
  uint64_t stream_start_{0};
  uint64_t available_time_{0};
  uint64_t base_time_{0}; // SmoothTree only, the lower start PTS time between all StreamIndex tags
  uint64_t live_delay_{0};
  bool has_timeshift_buffer_{false}; // Returns true when there is timeshift buffer for live content
  
  std::string m_supportedKeySystem;
  std::string location_;

  std::string current_pssh_; //! @todo: remove me
  std::string current_defaultKID_; //! @todo: remove me
  std::string current_iv_; //! @todo: remove me
  CryptoMode m_cryptoMode{CryptoMode::NONE};
  std::string license_url_; // SmoothTree only

  AdaptiveTree(CHOOSER::IRepresentationChooser* reprChooser);
  AdaptiveTree(const AdaptiveTree& left);
  virtual ~AdaptiveTree();

  /*!
   * \brief Configure the adaptive tree.
   * \param kodiProps The Kodi properties
   */
  virtual void Configure(const UTILS::PROPERTIES::KodiProperties& kodiProps);

  virtual bool open(const std::string& url) = 0;
  virtual bool open(const std::string& url, std::map<std::string, std::string> additionalHeaders) = 0;
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
                             const uint8_t* src,
                             std::string& dst,
                             size_t dstOffset,
                             size_t dataSize,
                             bool lastChunk);
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

  std::mutex &GetTreeMutex() { return treeMutex_; };
  bool HasUpdateThread() const
  {
    return updateThread_ != 0 && has_timeshift_buffer_ && updateInterval_ &&
           !m_manifestUpdateParam.empty();
  }
  void RefreshUpdateThread();
  const std::chrono::time_point<std::chrono::system_clock> GetLastUpdated() const { return lastUpdated_; };

  CHOOSER::IRepresentationChooser* GetRepChooser() { return m_reprChooser; }

  int SecondsSinceRepUpdate(PLAYLIST::CRepresentation* rep)
  {
    return static_cast<int>(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - GetRepLastUpdated(rep))
      .count());
  }

  virtual AdaptiveTree* Clone() const = 0;

  /*!
   * \brief Set the manifest update url parameter, used to force enabling manifest updates,
   *        by default by set the @param argument to "full" value, but the behaviour
   *        could change, it depends on the parser implementation.
   * \param manifestUrl The original manifest url value may be modified,
   *                    refer to each implementation of the parser.
   * \param param The update parameter, by default is accepted "full" value,
   *              refer to each implementation of the parser.
   */
  virtual void SetManifestUpdateParam(std::string& manifestUrl, std::string_view param)
  {
    m_manifestUpdateParam = param;
  }

  Settings m_settings;

protected:

  /*!
   * \brief Download manifest file.
   * \param url The url of the file to download
   * \param addHeaders Additional headers to add in the HTTP request
   * \param data [OUT] Return the HTTP response data
   * \param respHeaders [OUT] Return the HTTP response headers
   * \return True if has success, otherwise false
   */
  virtual bool DownloadManifest(std::string url,
                                const std::map<std::string, std::string>& addHeaders,
                                std::string& data,
                                HTTPRespHeaders& respHeaders);

  /*!
   * \brief Download a file (At each call feed also the repr. chooser
            to calculate the initial bandwidth).
   * \param url The url of the file to download
   * \param reqHeaders The headers to use in the HTTP request
   * \param data [OUT] Return the HTTP response data
   * \param respHeaders [OUT] Return the HTTP response headers
   * \return True if has success, otherwise false
   */
  virtual bool download(const std::string& url,
                        const std::map<std::string, std::string>& reqHeaders,
                        std::string& data,
                        HTTPRespHeaders& respHeaders);

  /*!
   * \brief Save manifest data to a file for debugging purpose.
   * \param fileNameSuffix Suffix to add to the filename generated.
   * \param data The manifest data to save.
   * \param info Additionals info to be add before the data.
   */
  virtual void SaveManifest(const std::string& fileNameSuffix,
                            std::string_view data,
                            std::string_view info);

  bool PreparePaths(const std::string &url);
  void SortTree();

  // Live segment update section
  virtual void StartUpdateThread();
  virtual void RefreshLiveSegments(){};

  uint32_t updateInterval_{~0U};
  std::mutex treeMutex_, updateMutex_;
  std::condition_variable updateVar_;
  std::thread* updateThread_{0};
  std::chrono::time_point<std::chrono::system_clock> lastUpdated_{std::chrono::system_clock::now()};
  std::string m_manifestParams;
  std::map<std::string, std::string> m_manifestHeaders;
  CHOOSER::IRepresentationChooser* m_reprChooser{nullptr};

  // Provide the path where the manifests will be saved, if debug enabled
  std::string m_pathSaveManifest;

private:
  void SegmentUpdateWorker();
};

} // namespace adaptive
