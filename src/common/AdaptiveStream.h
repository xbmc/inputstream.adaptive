/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AdaptiveTree.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace adaptive
{
  class AdaptiveStream;

  class ATTR_DLL_LOCAL AdaptiveStreamObserver
  {
  public:
    virtual void OnSegmentChanged(AdaptiveStream *stream) = 0;
    virtual void OnStreamChange(AdaptiveStream *stream) = 0;
  };

  class ATTR_DLL_LOCAL AdaptiveStream
  {
  public:
    AdaptiveStream(AdaptiveTree& tree,
                   AdaptiveTree::AdaptationSet* adp,
                   AdaptiveTree::Representation* initialRepr,
                   const UTILS::PROPERTIES::KodiProperties& kodiProps,
                   bool choose_rep_);
    virtual ~AdaptiveStream();
    void set_observer(AdaptiveStreamObserver *observer){ observer_ = observer; };
    void Reset();
    bool start_stream(uint64_t startPts =0);
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
    void info(std::ostream &s);
    uint64_t getMaxTimeMs();

    /*!
    * \brief Set the current segment to the one specified, and reset
    *   the buffer
    * \param newSegment The new segment
    */
    void ResetCurrentSegment(const AdaptiveTree::Segment* newSegment);

    unsigned int get_type()const{ return current_adp_->type_; };

    bool ensureSegment();
    uint32_t read(void* buffer, uint32_t  bytesToRead);
    uint64_t tell(){ read(0, 0);  return absolute_position_; };
    bool seek(uint64_t const pos);

   /*!
    * \brief Get the buffer size of the first segment in the buffer
    * \param size The segment buffer size
    * \return Return true if the size has been read, otherwise false
    */
    bool retrieveCurrentSegmentBufferSize(size_t& size);
    bool seek_time(double seek_seconds, bool preceeding, bool &needReset);
    AdaptiveTree::Period* getPeriod() { return current_period_; };
    AdaptiveTree::AdaptationSet* getAdaptationSet() { return current_adp_; };
    AdaptiveTree::Representation* getRepresentation() { return current_rep_; };
    size_t getSegmentPos() { return current_rep_->getCurrentSegmentPos(); };
    uint64_t GetCurrentPTSOffset() { return currentPTSOffset_; };
    uint64_t GetAbsolutePTSOffset() { return absolutePTSOffset_; };
    bool waitingForSegment(bool checkTime = false) const;
    void FixateInitialization(bool on);
    void SetSegmentFileOffset(uint64_t offset) { m_segmentFileOffset = offset; };
    bool StreamChanged() { return stream_changed_; }

    std::string GetStreamParams() const { return m_streamParams; }
    std::map<std::string, std::string> GetStreamHeaders() const { return m_streamHeaders; }

  protected:
    // Info to execute the download
    struct DownloadInfo
    {
      std::string m_url;
      std::map<std::string, std::string> m_addHeaders; // Additional headers
      uint64_t m_segmentNumber{0};
      uint16_t m_psshSet{0};
    };

    std::string m_streamParams;
    std::map<std::string, std::string> m_streamHeaders;

    virtual bool download(const DownloadInfo& downloadInfo,
                          std::string* lockfreeBuffer);
    virtual bool parseIndexRange(AdaptiveTree::Representation* rep, const std::string& buffer);
    bool write_data(const void* buffer,
                    size_t buffer_size,
                    std::string* lockfreeBuffer,
                    bool lastChunk,
                    const DownloadInfo& downloadInfo);
    virtual void SetLastUpdated(std::chrono::system_clock::time_point tm) {};
    std::chrono::time_point<std::chrono::system_clock> lastUpdated_;
    virtual bool download_segment(const DownloadInfo& downloadInfo);
    
    struct SEGMENTBUFFER
    {
      std::string buffer;
      AdaptiveTree::Segment segment;
      uint64_t segment_number;
      AdaptiveTree::Representation* rep;
    };
    std::vector<SEGMENTBUFFER> segment_buffers_;


  private:
    enum STATE
    {
      RUNNING,
      STOPPED,
      PAUSED
    } state_;

    // Segment download section
  
    void ResetSegment(const AdaptiveTree::Segment* segment);
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
    bool prepareNextDownload(DownloadInfo& downloadInfo);
    bool prepareDownload(const AdaptiveTree::Representation* rep,
                         const AdaptiveTree::Segment* seg,
                         uint64_t segNum,
                         DownloadInfo& downloadInfo);
    int SecondsSinceUpdate() const;
    static void ReplacePlaceholder(std::string& url, const std::string placeholder, uint64_t value);
    bool ResolveSegmentBase(AdaptiveTree::Representation* rep, bool stopWorker);

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
        thread_stop_ = true;
        signal_dl_.notify_one();
        if (download_thread_.joinable())
          download_thread_.join();
      };

      std::mutex mutex_rw_, mutex_dl_;
      std::condition_variable signal_rw_, signal_dl_;
      std::thread download_thread_;
      bool thread_stop_;
    };
    THREADDATA *thread_data_;

    AdaptiveTree &tree_;
    AdaptiveStreamObserver *observer_;
    // Active configuration
    AdaptiveTree::Period* current_period_;
    AdaptiveTree::AdaptationSet* current_adp_;
    AdaptiveTree::Representation *current_rep_;

    // Decrypter IV used to decrypt HLS segment
    // We need to store here because linked to representation
    uint8_t m_decrypterIv[16];

    static const size_t MAXSEGMENTBUFFER;
    // number of segmentbuffers whith valid segment, always >= valid_segment_buffers_
    size_t available_segment_buffers_;
    // number of segment_buffers which are downloaded / downloading
    uint32_t assured_buffer_length_;
    uint32_t max_buffer_length_; 
    size_t valid_segment_buffers_;
    uint32_t rep_counter_;
    AdaptiveTree::Representation *prev_rep_; // used for rep_counter_
    AdaptiveTree::Representation* last_rep_; // used to align new live rep with old

    std::size_t segment_read_pos_;
    uint64_t absolute_position_;
    uint64_t currentPTSOffset_, absolutePTSOffset_;

    std::atomic<bool> worker_processing_;
    bool m_fixateInitialization;
    uint64_t m_segmentFileOffset;
    bool play_timeshift_buffer_;
    bool stream_changed_ = false;
    bool choose_rep_;
  };
};
