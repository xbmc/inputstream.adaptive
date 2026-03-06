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
#include "decrypters/DrmEngine.h"
#include "decrypters/IDecrypter.h"
#include "utils/ResultType.h"

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
  SResult Initialize(std::string manifestUrl);

  bool CheckPlayableStreams(PLAYLIST::CPeriod* period);

  /*!
   * \brief Initialize adaptive tree period
   */
  void InitializePeriod();

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
   *  \param isDefaultVideoRepr Whether this video Representation is the default
   *  \param uniqueId A unique identifier for the Representation
   */
  void AddStream(PLAYLIST::CAdaptationSet* adp,
                 PLAYLIST::CRepresentation* repr,
                 bool isDefaultVideoRepr,
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
  bool PrepareStream(CStream& stream, uint64_t startPts);

  /*!
   * \brief Get a stream by index
   * \param index The stream index
   * \return The stream object if the index exists
   */
  std::shared_ptr<CStream> GetStream(unsigned int index) const;

  const std::vector<std::shared_ptr<CStream>>& GetStreams() const { return m_streams; }

  /*!
   * \brief Get the current (enabled) video stream
   * \return The video stream if found, otherwise nullptr
   */
  std::shared_ptr<CStream> GetCurrentVideoStream() const;

  /*! \brief Enable or disable a stream
   *  \param stream The stream object to act on
   *  \param enable Whether to enable or disable the stream
   */
  void EnableStream(std::shared_ptr<CStream> stream, bool enable);

  /*! \brief Get the number of streams in the session
   *  \return The number of streams in the session
   */
  unsigned int GetStreamCount() const { return static_cast<unsigned int>(m_streams.size()); }

  /*! \brief Get the media type mask
   *  \return The media type mask
   */
  uint8_t GetMediaTypeMask() const { return m_mediaTypeMask; }

  /*! \brief Get the total time in ms of the stream
   *  \return The total time in ms of the stream
   */
  uint64_t GetTotalTimeMs() const;

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

  /*!
   * \brief Seek streams and readers to a specified time.
   * \param seekTime The seek time in seconds.
   * \param isError Cannot seek due to unrecoverable error,
   *                in this case the method always return false.
   * \return True if the operation is successful (or seek through chapter/s), otherwise false
   */
  bool SeekTime(double seekTime, bool& isError);

  /*! \brief Report if the current content is dynamic/live
   *  \return True if live, false if VOD
   */
  bool IsLive() const { return m_adaptiveTree->IsLive(); };

  /*! \brief Get the mask of included streams
   *  \return A 32 uint with bits set of 'included' streams
   */
  uint32_t GetIncludedStreamMask() const;

  /*! \brief Get the chapter number currently being played
   *  \return 1 indexed vlaue of the current period
   */
  int GetChapter() const;

  /*! \brief Get the total amount of chapters
   *  \return The chapter count
   */
  int GetChapterCount() const;

  /*! \brief Get the chapter name for the specified chapter number.
   *  \param number The chapter number
   *  \return The chapter name if found, otherwise nullptr
   */
  const char* GetChapterName(int number) const;

  /*! \brief Get the chapter position in milliseconds
   *  \param number The chapter number
   *  \return The position in milliseconds of the chapter/period
   */
  int64_t GetChapterPos(int number) const;

  /*! \brief Seek to the chapter/period specified
   *  \param number The chapter number
   *  \return True if successful, false if invalid
   */
  bool SeekChapter(int number);

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

  const DRM::CDRMEngine& GetDRMEngine() const { return m_drmEngine; }

  /*!
   * \brief Get the stream ID relative to stream index in the current period.
   * \param streamIndex The stream index
   */
  int GetStreamIdFromIndex(int streamIndex) const;

  /*!
   * \brief Get the stream index relative to stream ID in the current period.
   * \param streamId The stream ID
   */
  unsigned int GetStreamIndexFromId(int streamId) const;

protected:
  /*!
   * \brief Get the index of the current period
   * \return The index of the current chapter/period
   */
  int GetPeriodIndex() const;

  /*!
   * \brief Determine the AdaptationSet that should be the default to be played,
   *        the behavior is mainly based on codec types
   * \param period The period where get adaptation sets
   * \return The AdaptationSet, or nullptr if unhandled
   */
  PLAYLIST::CAdaptationSet* DetermineDefaultAdpSet(PLAYLIST::CPeriod* period);

  /*!
   * \brief Get the media duration in ms, based on segments.
   */
  uint64_t GetMediaDurationMs();

private:
  uint64_t GetLiveEdgeMs() const;

  DRM::CDRMEngine m_drmEngine;

  adaptive::AdaptiveTree* m_adaptiveTree{nullptr};
  CHOOSER::IRepresentationChooser* m_reprChooser{nullptr};

  std::vector<std::shared_ptr<CStream>> m_streams;
  std::shared_ptr<CStream> m_timingStream;

  bool m_changed{false};
  uint64_t m_elapsedTime{0};
  uint64_t m_chapterStartTime{0}; // In STREAM_TIME_BASE
  double m_chapterSeekTime{0.0}; // In seconds
  uint8_t m_mediaTypeMask{0};
};
} // namespace SESSION
