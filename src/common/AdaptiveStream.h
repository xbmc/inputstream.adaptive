/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AdaptiveUtils.h"
#include "Segment.h"
#include "samplereader/SampleReader.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace PLAYLIST
{
class CAdaptationSet;
class CPeriod;
class CRepresentation;
}

namespace adaptive
{
// forward
class AdaptiveStream;
class AdaptiveTree;

// \brief Defines the type of event when starting the stream
enum class EVENT_TYPE
{
  NONE,
  STREAM_START, // First start of the stream
  STREAM_ENABLE, // Has been re-enabled the disabled stream
  REP_CHANGE // Has been changed representation (stream quality)
};

  class ATTR_DLL_LOCAL AdaptiveStreamObserver
  {
  public:
    virtual void OnSegmentChanged(AdaptiveStream *stream) = 0;
    virtual void OnStreamChange(AdaptiveStream *stream) = 0;
  };

  class ATTR_DLL_LOCAL AdaptiveStream : public SampleReaderObserver
  {
  public:
    AdaptiveStream(AdaptiveTree* tree,
                   PLAYLIST::CAdaptationSet* adpSet,
                   PLAYLIST::CRepresentation* initialRepr);
    virtual ~AdaptiveStream();
    void set_observer(AdaptiveStreamObserver *observer){ observer_ = observer; };
    void Reset();
    bool start_stream(const uint64_t startPts = 0);
    /*!
     * \brief Disable current representation, wait the current download is finished and stop downloads.
     */
    void Stop();
    void clear();
    /*!
     * \brief Delete download worker thread and his data,
     *        downloads must be already stopped with Stop() before call this method.
     */
    void DisposeWorker();
    uint64_t getMaxTimeMs();

    void Disable();

    EVENT_TYPE GetStartEvent() const { return m_startEvent; }

    /*!
    * \brief Set the current segment to the one specified, and reset
    *   the buffer
    * \param newSegment The new segment
    */
    void ResetCurrentSegment(const PLAYLIST::CSegment* newSegment);

    // Return the AP4_Track::Type based on current adaptation stream type
    int GetTrackType() const;
    PLAYLIST::StreamType GetStreamType() const;

    /*!
     * \brief Process that ensures that you have a segment that can be placed in the
     *        segments buffer for downloading, and that at same time a demuxer can read the data.
     * \return True when a segment exists and the data can be read (from segments buffer index 0), or
     *         False when there are no available segments, that could means:
     *         - End of stream
     *         - End of period/chapter for multiperiods streams
     *         - For live streams a delay to obtain segments from a manifest update
     */
    bool ensureSegment();

    uint32_t read(void* buffer, uint32_t bytesToRead);

    /*!
     * \brief Read the full stream buffer until EOF.
     * \param buffer[OUT] The full data buffer bytes
     * \return True if has success, otherwise false
     */
    bool ReadFullBuffer(std::vector<uint8_t>& buffer);

    uint64_t tell(){ read(0, 0);  return absolute_position_; };
    bool seek(uint64_t const pos);

   /*!
    * \brief Get the buffer size of the first segment in the buffer
    * \param size The segment buffer size
    * \return Return true if the size has been read, otherwise false
    */
    bool retrieveCurrentSegmentBufferSize(size_t& size);
    bool seek_time(double seek_seconds, bool preceeding, bool &needReset);
    PLAYLIST::CPeriod* getPeriod() { return current_period_; };
    PLAYLIST::CAdaptationSet* getAdaptationSet() { return current_adp_; };
    PLAYLIST::CRepresentation* getRepresentation() { return current_rep_; };
    size_t getSegmentPos();
    uint64_t GetCurrentPTSOffset() { return currentPTSOffset_; };
    uint64_t GetAbsolutePTSOffset() { return absolutePTSOffset_; };
    bool waitingForSegment() const;
    void FixateInitialization(bool on);
    void SetSegmentFileOffset(uint64_t offset) { m_segmentFileOffset = offset; };
    bool StreamChanged() { return m_startEvent == EVENT_TYPE::REP_CHANGE; }

    void OnTFRFatom(uint64_t ts, uint64_t duration, uint32_t mediaTimescale) override;

    /*!
    * \brief Some streaming manifest types can be required to create the Movie (MOOV) atom
    *        because its not provided with the stream.
    * \return True if its required to create Movie (MOOV) atom, otherwise false
    */
    bool IsRequiredCreateMovieAtom();

  protected:
    virtual bool parseIndexRange(PLAYLIST::CRepresentation* rep,
                                 const std::vector<uint8_t>& buffer);

    virtual void SetLastUpdated(const std::chrono::system_clock::time_point tm) {}
    std::chrono::time_point<std::chrono::system_clock> lastUpdated_;

    enum STATE
    {
      RUNNING,
      STOPPED,
      PAUSED
    } state_;

    // Segment download section

    struct SEGMENTBUFFER
    {
      std::vector<uint8_t> buffer;
      PLAYLIST::CSegment segment;
      uint64_t segment_number{0};
      PLAYLIST::CRepresentation* rep{nullptr};
    };
    // Be aware! All data related to segments stored in the SEGMENTBUFFER object are static,
    // these data are totally unrelated to manifest updates that may change the segments timeline,
    // so if you need to find a segment stored here in the timeline you must use segment number or PTS,
    // never by position otherwise you could cause misalignments.
    std::vector<SEGMENTBUFFER*> segment_buffers_;

    void AllocateSegmentBuffers(size_t size);
    void DeallocateSegmentBuffers();

    // Info to execute the download
    struct DownloadInfo
    {
      std::string m_url;
      std::map<std::string, std::string> m_addHeaders; // Additional headers
      SEGMENTBUFFER* m_segmentBuffer{nullptr}; // Optional, the segment buffer where to store the data
    };

    std::string m_streamParams;
    std::map<std::string, std::string> m_streamHeaders;

   /*!
    * \brief Download a file, the representation chooser will be updated with current download speed.
    * \param downloadInfo The info about the file to download
    * \param data[OUT] The downloaded data
    * \return Return true if success, otherwise false
    */
    virtual bool Download(const DownloadInfo& downloadInfo, std::vector<uint8_t>& data);

   /*!
    * \brief Download a segment file in chunks, the data could be also decrypted by the manifest parser,
    *        the download is done in chunks that will fill the segment buffer during the download,
    *        while at same time can be read by the demux reader, when the download is finished
    *        the representation chooser will be updated with current download speed.
    * \param downloadInfo The info about the file to download, its mandatory provide the segment buffer
    * \return Return true if success, otherwise false
    */
    virtual bool DownloadSegment(const DownloadInfo& downloadInfo);

   /*!
    * \brief Implementation to download a file.
    * \param downloadInfo The info about the file to download
    * \param data[OUT] If set, data will be stored on this variable, otherwise if nullptr the data
    *                  will be stored to the segment buffer and could be decrypted by the manifest parser.
    * \return Return true if success, otherwise false
    */
    bool DownloadImpl(const DownloadInfo& downloadInfo, std::vector<uint8_t>* data);

    bool PrepareNextDownload(DownloadInfo& downloadInfo);
    bool PrepareDownload(const PLAYLIST::CRepresentation* rep,
                         const PLAYLIST::CSegment& seg,
                         DownloadInfo& downloadInfo);

    void ResetSegment(const PLAYLIST::CSegment* segment);
    void ResetActiveBuffer(bool oneValid);
    /*!
     * \brief Wait for download in progress is completed, then stop the worker
     * \return True if success, otherwise false if meantime the worker status is changed
     */
    bool StopWorker(STATE state);
    /*!
     * \brief Wait until the worker become ready to manage next downloads
     */
    void WaitWorker();
    void worker();

    int SecondsSinceUpdate() const;

    bool GenerateSidxSegments(PLAYLIST::CRepresentation* rep);

    struct THREADDATA
    {
      THREADDATA()
        : thread_stop_(false)
      {
      }

      void Start(AdaptiveStream *parent)
      {
        download_thread_ = std::thread(&AdaptiveStream::worker, parent);
      }

      // \brief Stop the thread loop, make sure that dont enter in wait state again.
      void Stop()
      {
        thread_stop_ = true;
        signal_dl_.notify_one(); // Unlock possible condition variable signal_dl_ in "wait" state
      }

      ~THREADDATA()
      {
        Stop();
        if (download_thread_.joinable())
          download_thread_.join();
      };

      std::mutex mutex_rw_, mutex_dl_;
      std::condition_variable signal_rw_, signal_dl_;
      std::thread download_thread_;
      bool thread_stop_;
    };
    THREADDATA *thread_data_;

    AdaptiveTree* m_tree;
    AdaptiveStreamObserver *observer_;
    // Active configuration
    PLAYLIST::CPeriod* current_period_;
    PLAYLIST::CAdaptationSet* current_adp_;
    PLAYLIST::CRepresentation* current_rep_;
    PLAYLIST::CRepresentation* m_switchRep{nullptr};

    // Decrypter IV used to decrypt HLS segment
    // We need to store here because linked to representation
    uint8_t m_decrypterIv[16];

    // Minimum segment buffer size (segment_buffers_)
    uint32_t assured_buffer_length_{0};
    // The segment buffer size (segment_buffers_), so the max number of segments that can be downloaded and stored in memory
    uint32_t max_buffer_length_{0};
    // Number of segments stored in segment buffer (segment_buffers_) queued for downloading, always >= valid_segment_buffers_
    size_t available_segment_buffers_{0};
    // Number of segments stored in segment buffer (segment_buffers_) currently in download and downloaded
    size_t valid_segment_buffers_{0};

    std::size_t segment_read_pos_;
    uint64_t absolute_position_;
    uint64_t currentPTSOffset_, absolutePTSOffset_;

    std::atomic<bool> worker_processing_;
    bool m_fixateInitialization;
    uint64_t m_segmentFileOffset;

    // Defines the event to start the stream, the status will be resetted by start stream method.
    EVENT_TYPE m_startEvent{EVENT_TYPE::STREAM_START};

    // Class ID for debug log purpose, allow the LOG prints of each AdaptiveStream to be distinguished
    uint32_t clsId;
    static uint32_t globalClsId; // Incremental value for each new class created
  };
};
