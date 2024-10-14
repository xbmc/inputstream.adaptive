/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "Stream.h"
#include "common/AdaptiveStream.h"
#include "common/AdaptiveTree.h"
#include "decrypters/IDecrypter.h"

#if defined(ANDROID)
#include <kodi/platform/android/System.h>
#endif

#include <memory>

class Adaptive_CencSingleSampleDecrypter;

namespace SESSION
{
class ATTR_DLL_LOCAL CSession : public adaptive::AdaptiveStreamObserver
{
public:
  CSession() = default;
  virtual ~CSession();

  void DeleteStreams();

  /*! \brief Initialize the session
   *  \param manifestUrl The manifest URL
   *  \return True if has success, false otherwise
   */
  bool Initialize(std::string manifestUrl);

  /*
   * \brief Check HDCP parameters to remove unplayable representations
   */
  void CheckHDCP();

  /*! \brief Pre-Initialize the DRM
   *  \param challengeB64 [OUT] Provide the challenge data as base64
   *  \param sessionId [OUT] Provide the session ID
   *  \param isSessionOpened [OUT] Will be true if the DRM session has been opened
   *  \return True if has success, false otherwise
   */
  bool PreInitializeDRM(std::string& challengeB64, std::string& sessionId, bool& isSessionOpened);

  /*! \brief Initialize the DRM
   *  \param addDefaultKID Set True to add the default KID to the first session
   *  \return True if has success, false otherwise
   */
  bool InitializeDRM(bool addDefaultKID = false);

  /*! \brief Initialize adaptive tree period
   *  \param isSessionOpened Set True to kept and re-use the DRM session opened,
   *         otherwise False to reinitialize the DRM session
   *  \return True if has success, false otherwise
   */
  bool InitializePeriod(bool isSessionOpened = false);

  /*! \brief Get the sample reader of the next sample stream. This also set flags
   *         if the stream has changed, use CheckChange method to check it.
   *  \param sampleReader [OUT] Provide the sample reader with the lowest PTS value
   *         whose stream is enabled if found, otherwise means that we are waiting
   *         for the data, and the sample reader will not be provided.
   *  \return True if has success, otherwise false
   */
  bool GetNextSample(ISampleReader*& sampleReader);

  /*! \brief Create and push back a new Stream object
   *  \param adp The AdaptationSet of the stream
   *  \param repr The Representation of the stream
   *  \param isDefaultRepr Whether this Representation is the default
   *  \param uniqueId A unique identifier for the Representation
   */
  void AddStream(PLAYLIST::CAdaptationSet* adp,
                 PLAYLIST::CRepresentation* repr,
                 bool isDefaultRepr,
                 uint32_t uniqueId,
                 std::string_view audioLanguageOrig);

  /*! \brief Update stream's InputstreamInfo
   *  \param stream The stream to update
   */
  void UpdateStream(CStream& stream);

  /*!
   * \brief Update stream's InputstreamInfo
   * \param stream The stream to prepare
   */
  void PrepareStream(CStream* stream);

  /*! \brief Get a stream by index (starting at 1)
   *  \param sid The one-indexed stream id
   *  \return The stream object if the index exists
   */
  CStream* GetStream(unsigned int sid) const
  {
    return sid - 1 < m_streams.size() ? m_streams[sid - 1].get() : nullptr;
  }

  /*! \brief Enable or disable a stream
   *  \param stream The stream object to act on
   *  \param enable Whether to enable or disable the stream
   */
  void EnableStream(CStream* stream, bool enable);

  /*! \brief Get the number of streams in the session
   *  \return The number of streams in the session
   */
  unsigned int GetStreamCount() const { return static_cast<unsigned int>(m_streams.size()); }

  /*!
   * \brief Determines if the CDM session at specified index require Secure Path (TEE).
   * \return True if Secure Path is required, otherwise false.
   */
  bool IsCDMSessionSecurePath(size_t index);

  /*! \brief Get a session string (session id) by index from the cdm sessions
   *  \param index The index (psshSet number) of the cdm session
   *  \return The session string
   */
  std::string GetCDMSession(unsigned int index);

  /*! \brief Get the media type mask
   *  \return The media type mask
   */
  uint8_t GetMediaTypeMask() const { return m_mediaTypeMask; }

  /*! \brief Get a single sample decrypter by index from the cdm sessions
   *  \param index The index (psshSet number) of the cdm session
   *  \return The single sample decrypter
   */
  std::shared_ptr<Adaptive_CencSingleSampleDecrypter> GetSingleSampleDecryptor(unsigned int index) const
  {
    return m_cdmSessions[index].m_cencSingleSampleDecrypter;
  }

  /*! \brief Get the decrypter (DRM lib)
   *  \return The decrypter
   */
  DRM::IDecrypter* GetDecrypter() { return m_decrypter.get(); }

  /*! \brief Get a single sample decrypter matching the session id provided
   *  \param sessionId The session id string to match
   *  \return The single sample decrypter
   */
  std::shared_ptr<Adaptive_CencSingleSampleDecrypter> GetSingleSampleDecrypter(std::string sessionId);

  /*! \brief Get decrypter capabilities for a single sample decrypter
   *  \param index The index (psshSet number) of the cdm session
   *  \return The single sample decrypter capabilities
   */
  const DRM::DecrypterCapabilites& GetDecrypterCaps(unsigned int index) const
  {
    return m_cdmSessions[index].m_decrypterCaps;
  };

  /*! \brief Get the total time in ms of the stream
   *  \return The total time in ms of the stream
   */
  uint64_t GetTotalTimeMs() const { return m_adaptiveTree->m_totalTime; };

  /*! \brief Get the elapsed time in ms of the stream including all chapters
   *  \return The elapsed time in ms of the stream including all chapters
   */
  uint64_t GetElapsedTimeMs() const { return m_elapsedTime / 1000; };

  /*! \brief Provide a pts value that represents the time elapsed from current stream window
   *  \param pts The pts value coming from the stream reader
   *  \return The adjusted pts value
   */
  uint64_t PTSToElapsed(uint64_t pts);

  /*! \brief Get the start pts of the first segment in the timing stream
   *       with the difference in manifest time and reader time added
   *  \return The reader's timeshift buffer starting pts
   */
  uint64_t GetTimeshiftBufferStart();

  /*! \brief Align adaptiveStream and start stream reader
   *  \param stream The stream to start
   *  \param seekTime The pts value to start the stream at
   *  \param ptsDiff The pts difference to adjust seekTime by
   *  \param preceeding True to ask reader to seek to preceeding sync point
   *  \param timing True if this is the initial starting stream
   */
  void StartReader(
      CStream* stream, uint64_t seekTime, int64_t ptsDiff, bool preceeding, bool timing);

  /*! \brief Check if the stream has changed, reset changed status
   *  \param bSet True to keep m_changed value true
   *  \return True if stream has changed
   */
  bool CheckChange(bool bSet = false)
  {
    bool ret = m_changed;
    m_changed = bSet;
    return ret;
  };

  /*!
   * \brief Callback for screen resolution change.
   */
  void OnScreenResChange();

  /*! \brief Seek streams and readers to a specified time
   *  \param seekTime The seek time in seconds
   *  \param streamId ID of stream to seek, 0 seeks all
   *  \param preceeding True to seek to keyframe preceeding seektime,
   *         false to clamp to the start of the next segment
   *  \return True if seeking to another chapter or 1+ streams successfully
   *          seeked, false on error or no streams seeked
   */
  bool SeekTime(double seekTime, unsigned int streamId = 0, bool preceeding = true);

  /*! \brief Report if the current content is dynamic/live
   *  \return True if live, false if VOD
   */
  bool IsLive() const { return m_adaptiveTree->IsLive(); };

  /*! \brief Get the mask of included streams
   *  \return A 32 uint with bits set of 'included' streams
   */
  uint32_t GetIncludedStreamMask() const;


  /*! \brief Get the type crypto key system in use
   *  \return enum of crypto key system
   */
  STREAM_CRYPTO_KEY_SYSTEM GetCryptoKeySystem(std::string_view keySystem) const;

  /*! \brief Check if there is an initial discontinuity sequence number
   *  \return True if there is an initial discontinuity sequence number
   */
  bool HasInitialSequence() const
  {
    return m_adaptiveTree->initial_sequence_.has_value();
  }

  /*! \brief Get the initial discontinuity sequence number
   *  \return The sequence number of the first discontinuity sequence
   *          encountered when playback started
   */
  uint32_t GetInitialSequence() const
  {
    return m_adaptiveTree->initial_sequence_.has_value() ? *m_adaptiveTree->initial_sequence_ : 0;
  }

  /*! \brief Get the chapter number currently being played
   *  \return 1 indexed vlaue of the current period
   */
  int GetChapter() const;

  /*! \brief Get the total amount of chapters
   *  \return The chapter count
   */
  int GetChapterCount() const;

  /*! \brief Get the type crypto key system in use
   *  \param ch The index of chapter/period
   *  \return The id of the period
   */
  std::string GetChapterName(int ch) const;

  /*! \brief Get the chapter position in milliseconds
   *  \param ch The index (1 indexed) of chapter/period
   *  \return The position in milliseconds of the chapter/period
   */
  int64_t GetChapterPos(int ch) const;

  /*! \brief Get the id or sequence of the current period
   *  \return The id/sequence of the current chapter/period
   */
  int GetPeriodId() const;

  /*! \brief Seek to the chapter/period specified
   *  \param ch The index (1 indexed) of chapter/period
   *  \return True if successful, false if invalid
   */
  bool SeekChapter(int ch);

  /*! \brief Get start time of current chapter/period
   *  \return Time in us at start of current chapter/period
   */
  uint64_t GetChapterStartTime() const;

  /*! \brief Get value of m_chapterSeekTime
   *  \return Time stored in m_chapterSeekTime
   */
  double GetChapterSeekTime() { return m_chapterSeekTime; };

  /*! \brief Set m_chapterSeekTime back to 0
   */
  void ResetChapterSeekTime() { m_chapterSeekTime = 0; };

  /*!
   * \brief Callback from DemuxRead method of InputStream interface
   */
  void OnDemuxRead();

  //Observer Section

  /*! \brief Sets the current pts offset.
   *         To be called when the current segment of an adaptive stream changes.
   *  \param adStream The adaptive stream on which the segment has changed
   */
  void OnSegmentChanged(adaptive::AdaptiveStream* adStream) override;

  /*! \brief Sets the changed flag.
   *         To be called when the stream has changed/switched.
   *  \param adStream The adaptive stream on which the stream has changed
   */
  void OnStreamChange(adaptive::AdaptiveStream* adStream) override;

  /*!
   * \brief Callback from CInputStreamAdaptive::GetStream.
   * \param streamid The requested stream id
   * \param info The stream info object (can be updated)
   * \return True to allow Kodi core to load the stream, otherwise false
   */
  bool OnGetStream(int streamid, kodi::addon::InputstreamInfo& info);

protected:
  /*! \brief Check for and load decrypter module matching the supplied key system
   *  \param key_system [OUT] Will be assigned to if a decrypter is found matching
   *                    the set license type
   */
  void SetSupportedDecrypterURN(std::vector<std::string_view>& keySystems);

  /*! \brief Destroy all CencSingleSampleDecrypter instances
   */
  void DisposeSampleDecrypter();

  /*! \brief Destroy the decrypter module instance
   */
  void DisposeDecrypter();

  void ExtractStreamProtectionData(const PLAYLIST::CPeriod::PSSHSet& psshSet,
                                   std::string& defaultKid,
                                   std::vector<uint8_t>& initData,
                                   const std::vector<std::string_view>& keySystems);

private:
  std::shared_ptr<DRM::IDecrypter> m_decrypter;

  struct CCdmSession
  {
    DRM::DecrypterCapabilites m_decrypterCaps;
    std::shared_ptr<Adaptive_CencSingleSampleDecrypter> m_cencSingleSampleDecrypter;
    std::string m_sessionId;
  };
  std::vector<CCdmSession> m_cdmSessions;

  adaptive::AdaptiveTree* m_adaptiveTree{nullptr};
  CHOOSER::IRepresentationChooser* m_reprChooser{nullptr};

  std::vector<std::unique_ptr<CStream>> m_streams;
  CStream* m_timingStream{nullptr};

  bool m_changed{false};
  uint64_t m_elapsedTime{0};
  uint64_t m_chapterStartTime{0}; // In STREAM_TIME_BASE
  double m_chapterSeekTime{0.0}; // In seconds
  uint8_t m_mediaTypeMask{0};
};
} // namespace SESSION
