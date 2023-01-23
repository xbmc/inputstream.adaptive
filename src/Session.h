/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "KodiHost.h"
#include "Stream.h"
#include "common/AdaptiveStream.h"
#include "common/AdaptiveTree.h"
#include "common/Chooser.h"
#include "samplereader/SampleReader.h"
#include "utils/PropertiesUtils.h"

#include <bento4/Ap4.h>
#include <kodi/tools/DllHelper.h>

class Adaptive_CencSingleSampleDecrypter;

namespace SESSION
{
class ATTR_DLL_LOCAL CSession : public adaptive::AdaptiveStreamObserver
{
public:
  CSession(const UTILS::PROPERTIES::KodiProperties& properties,
           const std::string& manifestUrl,
           const std::string& profilePath);
  virtual ~CSession();

  /*! \brief Set the DRM configuration flags
   *  \param config Flags to be passed to the decrypter
   */
  void SetDrmConfig(const std::uint8_t config) { m_drmConfig = config; }

  /*! \brief Initialize the session
   *  \return True if has success, false otherwise
   */
  bool Initialize();

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
  void AddStream(adaptive::AdaptiveTree::AdaptationSet* adp,
                 adaptive::AdaptiveTree::Representation* repr,
                 bool isDefaultRepr,
                 uint32_t uniqueId);

  /*! \brief Update stream's InputstreamInfo
   *  \param stream The stream to update
   */
  void UpdateStream(CStream& stream);

  /*! \brief Update stream's InputstreamInfo
   *  \param stream The stream to prepare
   *  \param needRefetch [OUT] Set to true if stream info has changed
   *  \return The Movie box if the stream container is MP4 and a valid MOOV
   *        box is found, otherwise nullptr
   */
  AP4_Movie* PrepareStream(CStream* stream, bool& needRefetch);


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
  unsigned int GetStreamCount() const { return m_streams.size(); }

  /*! \brief Get a session string (session id) by index from the cdm sessions
   *  \param index The index (psshSet number) of the cdm session
   *  \return The session string
   */
  const char* GetCDMSession(unsigned int index) { return m_cdmSessions[index].m_cdmSessionStr; }

  /*! \brief Get the media type mask
   *  \return The media type mask
   */
  uint8_t GetMediaTypeMask() const { return m_mediaTypeMask; }

  /*! \brief Get a single sample decrypter by index from the cdm sessions
   *  \param index The index (psshSet number) of the cdm session
   *  \return The single sample decrypter
   */
  Adaptive_CencSingleSampleDecrypter* GetSingleSampleDecryptor(unsigned int index) const
  {
    return m_cdmSessions[index].m_cencSingleSampleDecrypter;
  }

  /*! \brief Get the decrypter (SSD lib)
   *  \return The decrypter
   */
  SSD::SSD_DECRYPTER* GetDecrypter() { return m_decrypter; }

  /*! \brief Get a single sample decrypter matching the session id provided
   *  \param sessionId The session id string to match
   *  \return The single sample decrypter
   */
  Adaptive_CencSingleSampleDecrypter* GetSingleSampleDecrypter(std::string sessionId);

  /*! \brief Get decrypter capabilities for a single sample decrypter
   *  \param index The index (psshSet number) of the cdm session
   *  \return The single sample decrypter capabilities
   */
  const SSD::SSD_DECRYPTER::SSD_CAPS& GetDecrypterCaps(unsigned int index) const
  {
    return m_cdmSessions[index].m_decrypterCaps;
  };

  /*! \brief Get the total time in ms of the stream
   *  \return The total time in ms of the stream
   */
  uint64_t GetTotalTimeMs() const { return m_adaptiveTree->overallSeconds_ * 1000; };

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

  /*! \brief To inform of Kodi's current screen resolution
   *  \param width The width in pixels
   *  \param height The height in pixels
   */
  void SetVideoResolution(int width, int height, int maxWidth, int maxHeight);

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
  bool IsLive() const { return m_adaptiveTree->has_timeshift_buffer_; };

  /*! \brief Get the type of manifest being played
   *  \return ManifestType - MPD/ISM/HLS
   */
  UTILS::PROPERTIES::ManifestType GetManifestType() const { return m_kodiProps.m_manifestType; }

  /*! \brief Get the default keyid for a pssh
   *  \param index The psshset number to retrieve from
   *  \return The default keyid
   */
  const AP4_UI08* GetDefaultKeyId(const uint16_t index) const;


  /*! \brief Get the mask of included streams
   *  \return A 32 uint with bits set of 'included' streams
   */
  uint32_t GetIncludedStreamMask() const;


  /*! \brief Get the type crypto key system in use
   *  \return enum of crypto key system
   */
  STREAM_CRYPTO_KEY_SYSTEM GetCryptoKeySystem() const;

  /*! \brief Get the initial discontinuity sequence number
   *  \return The sequence number of the first discontinuity sequence
   *          encountered when playback started
   */
  uint32_t GetInitialSequence() const { return m_adaptiveTree->initial_sequence_; }

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
  const char* GetChapterName(int ch) const;

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

protected:
  /*! \brief If available, read the duration and timestamp of the next fragment and
   *         set the related members
   *  \param adStream [OUT] The adaptive stream to check
   */
  void CheckFragmentDuration(CStream& stream);

  /*! \brief Check for and load decrypter module matching the supplied key system
   *  \param key_system [OUT] Will be assigned to if a decrypter is found matching
   *                    the set license type
   */
  void SetSupportedDecrypterURN(std::string& key_system);

  /*! \brief Destroy all CencSingleSampleDecrypter instances
   */
  void DisposeSampleDecrypter();

  /*! \brief Destroy the decrypter module instance
   */
  void DisposeDecrypter();

private:
  const UTILS::PROPERTIES::KodiProperties m_kodiProps;
  std::string m_manifestUrl;
  AP4_DataBuffer m_serverCertificate;
  std::unique_ptr<kodi::tools::CDllHelper> m_dllHelper;
  SSD::SSD_DECRYPTER* m_decrypter{nullptr};

  struct CCdmSession
  {
    SSD::SSD_DECRYPTER::SSD_CAPS m_decrypterCaps;
    Adaptive_CencSingleSampleDecrypter* m_cencSingleSampleDecrypter;
    const char* m_cdmSessionStr = nullptr;
    bool m_sharedCencSsd{false};
  };
  std::vector<CCdmSession> m_cdmSessions;

  adaptive::AdaptiveTree* m_adaptiveTree{nullptr};
  CHOOSER::IRepresentationChooser* m_reprChooser;

  std::vector<std::unique_ptr<CStream>> m_streams;
  CStream* m_timingStream{nullptr};

  bool m_changed{false};
  uint64_t m_elapsedTime{0};
  uint64_t m_chapterStartTime{0}; // In STREAM_TIME_BASE
  double m_chapterSeekTime{0.0}; // In seconds
  uint8_t m_mediaTypeMask{0};
  uint8_t m_drmConfig{0};
  bool m_settingNoSecureDecoder{false};
  bool m_settingIsHdcpOverride{false};
  bool m_firstPeriodInitialized{false};
  std::unique_ptr<CKodiHost> m_KodiHost;
};
} // namespace SESSION
