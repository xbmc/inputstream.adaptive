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
#include "SegmentBuffer.h"
#include "samplereader/SampleReader.h"

#include <atomic>
#include <condition_variable>
#include <chrono>
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
    bool start_stream();
    /*!
     * \brief Disable current representation, wait the current download is finished and stop downloads.
     */
    void Stop();
    void clear();
    /*!
     * \brief Dispose resources.
     *        When this method is called, it is assumed that: AdaptiveStream::Stop has been already called
     *        and that the relative sample reader is already stopped, to avoid data access violations.
     */
    void Dispose();
    uint64_t getMaxTimeMs();

    void Disable();

    EVENT_TYPE GetStartEvent() const { return m_startEvent; }

    /*!
     * \brief Update the current segment to the specified one, and align or reset the buffer
     * \param newSegment The new current segment
     */
    void UpdateCurrentSegment(const PLAYLIST::CSegment& newSegment);

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

    bool GetBufferSize(uint64_t& size);
    uint64_t tell();

    /*!
     * \brief Seek the stream to the specified data position on the current segment.
     * \param pos The data position to seek, is the sum of the absolute position and any additional bytes to be seek for
     * \param isEos[OUT] The value will be changed to true, only when the stream is end
     * \return True if has success, otherwise false (it should cause EOS in the sample reader demuxer)
     */
    bool seek(uint64_t const pos, bool& isEos);

    bool seek_time(double seek_seconds);
    PLAYLIST::CPeriod* getPeriod() { return current_period_; };
    PLAYLIST::CAdaptationSet* getAdaptationSet() { return current_adp_; };
    PLAYLIST::CRepresentation* getRepresentation() { return current_rep_; };

    uint64_t GetCurrentPTSOffset() const { return currentPTSOffset_; }
    uint64_t GetAbsolutePTSOffset() const { return absolutePTSOffset_; }

    bool OnSampleRequested();

    void FixateInitialization(bool on);
    void AllowBufferQueue(bool isAllowed) { m_isAllowedBufferQueue = isAllowed; }
    void SetSegmentFileOffset(uint64_t offset) { m_segmentFileOffset = offset; };
    bool StreamChanged() { return m_startEvent == EVENT_TYPE::REP_CHANGE; }

    void OnTFRFatom(uint64_t ts, uint64_t duration, uint32_t mediaTimescale) override;

    /*!
    * \brief Some streaming manifest types can be required to create the Movie (MOOV) atom
    *        because its not provided with the stream.
    * \return True if its required to create Movie (MOOV) atom, otherwise false
    */
    bool IsRequiredCreateMovieAtom();

    bool IsWaitingForSegment() const { return m_isWaitingForSegment; }

  protected:
    virtual bool parseIndexRange(PLAYLIST::CRepresentation* rep,
                                 const std::vector<uint8_t>& buffer);

    virtual void SetLastUpdated(const std::chrono::system_clock::time_point tm) {}
    std::chrono::time_point<std::chrono::system_clock> lastUpdated_;

    // Segment download section

    ADP::CSegmentBuffers m_segBuffers;

    // Info to execute the download
    struct DownloadInfo
    {
      std::string m_url;
      std::map<std::string, std::string> m_addHeaders; // Additional headers
      uint64_t m_rangeBegin{PLAYLIST::NO_VALUE};
      uint64_t m_rangeEnd{PLAYLIST::NO_VALUE};
      ADP::SegmentBuffer* m_segmentBuffer{nullptr}; // Optional, the segment buffer where to store the data
    };

    std::string m_streamParams;
    std::map<std::string, std::string> m_streamHeaders;

    bool m_isWaitingForSegment{false};
    mutable std::chrono::time_point<std::chrono::steady_clock> m_tsOnSampleRequest{};

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

    void ResetSegment(const PLAYLIST::CSegment& segment);
    
    /*!
     * \brief Align the buffer to the specified segment, if it exists in the buffer, otherwise empty the buffer.
     * \param segment The segment to align the buffer
     * \return Return true if the buffer contains the segment, otherwise false
     */
    bool AlignBufferToSegment(const PLAYLIST::CSegment& segment);

    void worker();

    int SecondsSinceUpdate() const;

    bool GenerateSidxSegments(PLAYLIST::CRepresentation* rep);

    struct THREADDATA
    {
      THREADDATA() = default;

      // \brief Initialize the thread and start downloads
      void Initialize(AdaptiveStream* parent);

      ~THREADDATA()
      {
        m_isThreadExit = true;
        cvState.notify_all();

        if (m_downloadThread.joinable())
          m_downloadThread.join();
      };

      std::mutex mutexRW; // Mutex for buffer reading/writing
      std::condition_variable cvRW; // Condition variable for reading/writing in the currently downloading segment buffer

      std::mutex mutexWorker; // Mutex used when the worker is busy processing a download
      std::condition_variable cvState; // Used to signal a change of ThState

      // \brief Thread state
      enum class ThState
      {
        RUNNING, // Process downloads in queue
        PAUSED, // Complete the current download, then pause the next downloads in the queue
        STOPPED, // Cancel the current download, then pause the next downloads in the queue
      };

      // \brief Determines the current m_state of the worker thread
      ThState State() const { return m_state; }
      // \brief Determines whether the thread is about to be stopped permanently
      bool IsThreadExit() const { return m_isThreadExit; }
      // \brief Stop downloads, and cancel download in progress (it dont delete buffer queue)
      void StopDownloads();
      // \brief Pause downloads on next iteration, without cancel download in progress (it dont delete buffer queue)
      void PauseDownloads();
      // \brief Start downloads
      void StartDownloads();

    private:
      std::thread m_downloadThread;
      bool m_isThreadExit{false};
      std::atomic<ThState> m_state = ThState::STOPPED;
    };
    THREADDATA* thread_data_{nullptr};

    AdaptiveTree* m_tree;
    AdaptiveStreamObserver* observer_{nullptr};
    // Active configuration
    PLAYLIST::CPeriod* current_period_;
    PLAYLIST::CAdaptationSet* current_adp_;
    PLAYLIST::CRepresentation* current_rep_;
    PLAYLIST::CRepresentation* m_switchRep{nullptr};

    // Decrypter IV used to decrypt HLS segment
    // We need to store here because linked to representation
    uint8_t m_decrypterIv[16]{0};

    // Minimum segment buffer size (segment_buffers_)
    uint32_t assured_buffer_length_{0};
    // The segment buffer size (segment_buffers_), so the max number of segments that can be downloaded and stored in memory
    uint32_t max_buffer_length_{0};

    std::size_t segment_read_pos_{0};
    uint64_t absolute_position_{0}; // The absolute position is the segment range begin (if available), and will be increased with each reading
    uint64_t currentPTSOffset_{0};
    uint64_t absolutePTSOffset_{0};

    bool m_fixateInitialization{false};
    bool m_isAllowedBufferQueue{true};
    uint64_t m_segmentFileOffset{0};

    // Defines the event to start the stream, the status will be resetted by start stream method.
    EVENT_TYPE m_startEvent{EVENT_TYPE::STREAM_START};

    // Class ID for debug log purpose, allow the LOG prints of each AdaptiveStream to be distinguished
    uint32_t clsId;
    static uint32_t globalClsId; // Incremental value for each new class created
  };
};
