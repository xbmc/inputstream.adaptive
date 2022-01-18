/*
*      Copyright (C) 2016-2016 peak3d
*      http://www.peak3d.de
*
*  This Program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This Program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  <http://www.gnu.org/licenses/>.
*
*/

#pragma once

#include "AdaptiveTree.h"

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include <kodi/AddonBase.h>

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
                   const std::map<std::string, std::string>& media_headers,
                   bool play_timeshift_buffer,
                   size_t repId,
                   bool choose_rep_);
    virtual ~AdaptiveStream();
    void set_observer(AdaptiveStreamObserver *observer){ observer_ = observer; };
    void Reset();
    bool start_stream();
    void stop();
    void clear();
    void info(std::ostream &s);
    uint64_t getMaxTimeMs();

    unsigned int get_type()const{ return current_adp_->type_; };

    bool ensureSegment();
    uint32_t read(void* buffer, uint32_t  bytesToRead);
    uint64_t tell(){ read(0, 0);  return absolute_position_; };
    bool seek(uint64_t const pos);
    bool getSize(unsigned long long& sz);
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
  protected:
    virtual bool download(const char* url,
                          const std::map<std::string, std::string>& mediaHeaders,
                          std::string* lockfreeBuffer)
    {
      return false;
    };
    virtual bool parseIndexRange(AdaptiveTree::Representation* rep, const std::string& buffer)
    {
      return false;
    };
    bool write_data(const void* buffer, size_t buffer_size, std::string* lockfreeBuffer);
    virtual void SetLastUpdated(std::chrono::system_clock::time_point tm) {};
    std::chrono::time_point<std::chrono::system_clock> lastUpdated_;
    virtual bool download_segment();
    std::string download_url_;
    std::map<std::string, std::string> media_headers_, download_headers_;
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
    void StopWorker(STATE state);
    void worker();
    bool prepareNextDownload();
    bool prepareDownload(const AdaptiveTree::Representation* rep,
                         const AdaptiveTree::Segment* seg,
                         uint64_t segNum);
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

      ~THREADDATA()
      {
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

    uint16_t download_pssh_set_;
    uint64_t download_segNum_;
    bool worker_processing_;
    uint8_t m_iv[16];
    bool m_fixateInitialization;
    uint64_t m_segmentFileOffset;
    bool play_timeshift_buffer_;
    bool stream_changed_ = false;
    bool choose_rep_;
  };
};
