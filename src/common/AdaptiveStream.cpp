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

#include "AdaptiveStream.h"

#include <iostream>
#include <cstring>
#include "../oscompat.h"
#include "../log.h"
#include <math.h>

using namespace adaptive;

AdaptiveStream::AdaptiveStream(AdaptiveTree &tree, AdaptiveTree::StreamType type)
  :thread_data_(nullptr)
  , tree_(tree)
  , type_(type)
  , observer_(nullptr)
  , current_period_(tree_.periods_.empty() ? nullptr : tree_.periods_[0])
  , current_adp_(nullptr)
  , current_rep_(nullptr)
  , segment_read_pos_(0)
  , currentPTSOffset_(0)
  , absolutePTSOffset_(0)
  , lastUpdated_(std::chrono::system_clock::now())
{
}

AdaptiveStream::~AdaptiveStream()
{
  stop();
  clear();
}

void AdaptiveStream::ResetSegment()
{
  segment_buffer_.clear();
  segment_read_pos_ = 0;

  if (current_rep_->current_segment_ && !(current_rep_->flags_ & (AdaptiveTree::Representation::SEGMENTBASE
  | AdaptiveTree::Representation::TEMPLATE | AdaptiveTree::Representation::URLSEGMENTS)))
    absolute_position_ = current_rep_->current_segment_->range_begin_;
}

bool AdaptiveStream::download_segment()
{
  if (download_url_.empty())
    return false;

  return download(download_url_.c_str(), download_headers_);
}

void AdaptiveStream::worker()
{
  std::unique_lock<std::mutex> lckdl(thread_data_->mutex_dl_);
  thread_data_->signal_dl_.notify_one();
  do {
    thread_data_->signal_dl_.wait(lckdl);

    bool ret(download_segment());
    unsigned int retryCount(10);

    while (!ret && !stopped_ && retryCount-- && tree_.has_timeshift_buffer_)
    {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      Log(LOGLEVEL_DEBUG, "AdaptiveStream: trying to reload segment ...");
      ret = download_segment();
    }

    //Signal finished download
    {
      std::lock_guard<std::mutex> lckrw(thread_data_->mutex_rw_);
      download_url_.clear();
      if (!ret)
        stopped_ = true;
    }
    thread_data_->signal_rw_.notify_one();

  } while (!thread_data_->thread_stop_);
}

int AdaptiveStream::SecondsSinceUpdate() const
{
  const std::chrono::time_point<std::chrono::system_clock> &tPoint(lastUpdated_ > tree_.GetLastUpdated() ? lastUpdated_ : tree_.GetLastUpdated());
  return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - tPoint).count());
}

bool AdaptiveStream::write_data(const void *buffer, size_t buffer_size)
{
  {
    std::lock_guard<std::mutex> lckrw(thread_data_->mutex_rw_);

    if (stopped_)
      return false;

    size_t insertPos(segment_buffer_.size());
    segment_buffer_.resize(insertPos + buffer_size);
    tree_.OnDataArrived(download_segNum_, download_pssh_set_, m_iv, reinterpret_cast<const uint8_t*>(buffer),
      reinterpret_cast<uint8_t*>(&segment_buffer_[0]), insertPos, buffer_size);
  }
  thread_data_->signal_rw_.notify_one();
  return true;
}

bool AdaptiveStream::prepare_stream(const AdaptiveTree::AdaptationSet *adp,
  const uint32_t width, const uint32_t height, uint32_t hdcpLimit, uint16_t hdcpVersion,
  uint32_t min_bandwidth, uint32_t max_bandwidth, unsigned int repId,
  const std::map<std::string, std::string> &media_headers)
{
  width_ = type_ == AdaptiveTree::VIDEO ? width : 0;
  height_ = type_ == AdaptiveTree::VIDEO ? height : 0;
  hdcpLimit_ = hdcpLimit;
  hdcpVersion_ = hdcpVersion;

  uint32_t avg_bandwidth = tree_.bandwidth_;

  bandwidth_ = min_bandwidth;
  if (avg_bandwidth > bandwidth_)
    bandwidth_ = avg_bandwidth;
  if (max_bandwidth && bandwidth_ > max_bandwidth)
    bandwidth_ = max_bandwidth;

  stopped_ = false;

  bandwidth_ = static_cast<uint32_t>(bandwidth_ *(type_ == AdaptiveTree::VIDEO ? 0.9 : 0.1));

  current_adp_ = adp;

  media_headers_ = media_headers;

  return select_stream(false, true, repId);
}

bool AdaptiveStream::start_stream(const uint32_t seg_offset, uint16_t width, uint16_t height)
{
  if (!~seg_offset && tree_.has_timeshift_buffer_ && current_rep_->segments_.data.size()>1)
  {
    std::int32_t pos;
    if (tree_.has_timeshift_buffer_ || tree_.available_time_>= tree_.stream_start_)
      pos = static_cast<int32_t>(current_rep_->segments_.data.size() - 1);
    else
    {
      pos = static_cast<int32_t>(((tree_.stream_start_ - tree_.available_time_)*current_rep_->timescale_) / current_rep_->duration_);
      if (!pos) pos = 1;
    }
    //go at least 12 secs back
    uint64_t duration(current_rep_->get_segment(pos)->startPTS_ - current_rep_->get_segment(pos - 1)->startPTS_);
    pos -= static_cast<uint32_t>((12 * current_rep_->timescale_) / duration) + 1;
    current_rep_->current_segment_ = current_rep_->get_segment(pos < 0 ? 0: pos);
  }
  else
    current_rep_->current_segment_ = ~seg_offset ? current_rep_->get_segment(seg_offset) : 0;

  segment_buffer_.clear();
  segment_read_pos_ = 0;

  if (!current_rep_->get_next_segment(current_rep_->current_segment_))
  {
    absolute_position_ = ~0;
    stopped_ = true;
  }
  else
  {
    width_ = type_ == AdaptiveTree::VIDEO ? width : 0;
    height_ = type_ == AdaptiveTree::VIDEO ? height : 0;

    if (!(current_rep_->flags_ & (AdaptiveTree::Representation::SEGMENTBASE | AdaptiveTree::Representation::TEMPLATE | AdaptiveTree::Representation::URLSEGMENTS)))
      absolute_position_ = current_rep_->get_next_segment(current_rep_->current_segment_)->range_begin_;
    else
      absolute_position_ = 0;

    stopped_ = false;
  }

  if (!thread_data_)
  {
    thread_data_ = new THREADDATA();
    std::unique_lock<std::mutex> lckdl(thread_data_->mutex_dl_);
    thread_data_->Start(this);
    // Wait until worker thread is waiting for input
    thread_data_->signal_dl_.wait(lckdl);
  }

  return true;
}

bool AdaptiveStream::restart_stream()
{
  if (!start_stream(~0, width_, height_))
    return false;

  /* lets download the initialization */
  if (PrepareDownload(current_rep_->get_initialization()) && !download_segment())
    return false;
  download_url_.clear();

  return true;
}

bool AdaptiveStream::PrepareDownload(const AdaptiveTree::Segment *seg)
{
  if (!seg)
    return false;

  currentPTSOffset_ = (seg->startPTS_ * current_rep_->timescale_ext_) / current_rep_->timescale_int_;
  absolutePTSOffset_ = (current_rep_->segments_[0]->startPTS_ * current_rep_->timescale_ext_) / current_rep_->timescale_int_;

  if (observer_ && seg != &current_rep_->initialization_)
    observer_->OnSegmentChanged(this);

  char rangebuf[128], *rangeHeader(0);

  if (!(current_rep_->flags_ & AdaptiveTree::Representation::SEGMENTBASE))
  {
    if (!(current_rep_->flags_ & AdaptiveTree::Representation::TEMPLATE))
    {
      if (current_rep_->flags_ & AdaptiveTree::Representation::URLSEGMENTS)
      {
        download_url_ = seg->url;
        if (download_url_.find("://", 0) == std::string::npos)
          download_url_ = current_rep_->url_ + download_url_;
      }
      else
      {
        download_url_ = current_rep_->url_;
        sprintf(rangebuf, "bytes=%" PRIu64 "-%" PRIu64, seg->range_begin_, seg->range_end_);
        rangeHeader = rangebuf;
      }
    }
    else if (seg != &current_rep_->initialization_) //templated segment
    {
      std::string media = current_rep_->segtpl_.media;
      std::string::size_type lenReplace(7);
      std::string::size_type np(media.find("$Number"));
      uint64_t value(seg->range_end_); //StartNumber

      if (np == std::string::npos)
      {
        lenReplace = 5;
        np = media.find("$Time");
        value = seg->range_begin_; //Timestamp
      }
      np += lenReplace;

      std::string::size_type npe(media.find('$', np));

      char fmt[16];
      if (np == npe)
        strcpy(fmt, "%" PRIu64);
      else
        strcpy(fmt, media.substr(np, npe - np).c_str());

      sprintf(rangebuf, fmt, value);
      media.replace(np - lenReplace, npe - np + lenReplace + 1, rangebuf);
      download_url_ = media;
    }
    else //templated initialization segment
      download_url_ = current_rep_->url_;
  }
  else
  {
    download_url_ = current_rep_->url_;
    sprintf(rangebuf, "bytes=%" PRIu64 "-%" PRIu64, seg->range_begin_, seg->range_end_);
    rangeHeader = rangebuf;
  }

  download_segNum_ = current_rep_->startNumber_ + current_rep_->get_segment_pos(seg);
  download_pssh_set_ = seg->pssh_set_;
  download_headers_ = media_headers_;
  if (rangeHeader)
    download_headers_["Range"] = rangeHeader;
  else
    download_headers_.erase("Range");

  if (!tree_.effective_url_.empty() && download_url_.find(tree_.base_url_) == 0)
    download_url_.replace(0, tree_.base_url_.size(), tree_.effective_url_);

  return true;
}

bool AdaptiveStream::ensureSegment()
{
  if (stopped_)
    return false;

  if (download_url_.empty() && segment_read_pos_ >= segment_buffer_.size())
  {
    //wait until worker is ready for new segment
    std::lock_guard<std::mutex> lck(thread_data_->mutex_dl_);
    std::lock_guard<std::mutex> lckTree(tree_.GetTreeMutex());

    if (tree_.HasUpdateThread() && SecondsSinceUpdate() > 1)
    {
      tree_.RefreshSegments(current_rep_, current_adp_->type_);
      lastUpdated_ = std::chrono::system_clock::now();
    }

    const AdaptiveTree::Segment *nextSegment = current_rep_->get_next_segment(current_rep_->current_segment_);
    if (nextSegment)
    {
      current_rep_->current_segment_ = nextSegment;
      PrepareDownload(nextSegment);
      ResetSegment();
      thread_data_->signal_dl_.notify_one();
    }
    else if (tree_.HasUpdateThread())
    {
      current_rep_->flags_ |= AdaptiveTree::Representation::WAITFORSEGMENT;
      Log(LOGLEVEL_DEBUG, "Begin WaitForSegment stream %s", current_rep_->id.c_str());
      return false;
    }
    else
    {
      stopped_ = true;
      return false;
    }
  }
  return true;
}


uint32_t AdaptiveStream::read(void* buffer, uint32_t  bytesToRead)
{
  std::unique_lock<std::mutex> lckrw(thread_data_->mutex_rw_);

NEXTSEGMENT:
  if (!stopped_ && ensureSegment() && bytesToRead)
  {
    while (true)
    {
      uint32_t avail = segment_buffer_.size() - segment_read_pos_;
      if (avail < bytesToRead && !download_url_.empty())
      {
        thread_data_->signal_rw_.wait(lckrw);
        continue;
      }

      if (avail > bytesToRead)
        avail = bytesToRead;

      segment_read_pos_ += avail;
      absolute_position_ += avail;

      if (avail == bytesToRead)
      {
        memcpy(buffer, segment_buffer_.data() + (segment_read_pos_ - avail), avail);
        return avail;
      }
      // If we call read after the last chunk was read but before worker finishes download, we end up here.
      if (!avail)
        goto NEXTSEGMENT;
      return 0;
    }
  }
  return 0;
}

bool AdaptiveStream::seek(uint64_t const pos)
{
  std::unique_lock<std::mutex> lckrw(thread_data_->mutex_rw_);
  // we seek only in the current segment
  if (!stopped_ && pos >= absolute_position_ - segment_read_pos_)
  {
    segment_read_pos_ = static_cast<uint32_t>(pos - (absolute_position_ - segment_read_pos_));

    while (segment_read_pos_ > segment_buffer_.size() && !download_url_.empty())
      thread_data_->signal_rw_.wait(lckrw);

    if (segment_read_pos_ > segment_buffer_.size())
    {
      segment_read_pos_ = static_cast<uint32_t>(segment_buffer_.size());
      return false;
    }
    absolute_position_ = pos;
    return true;
  }
  return false;
}

bool AdaptiveStream::seek_time(double seek_seconds, bool preceeding, bool &needReset)
{
  if (!current_rep_)
    return false;

  if (stopped_)
    // For subtitles which come in one file we should return true!
    return current_rep_->segments_.empty();

  if (current_rep_->flags_ & AdaptiveTree::Representation::SUBTITLESTREAM)
    return true;

  std::unique_lock<std::mutex> lckTree(tree_.GetTreeMutex());

  uint32_t choosen_seg(~0);

  uint64_t sec_in_ts = static_cast<uint64_t>(seek_seconds * current_rep_->timescale_);
  choosen_seg = 0; //Skip initialization
  while (choosen_seg < current_rep_->segments_.data.size() && sec_in_ts > current_rep_->get_segment(choosen_seg)->startPTS_)
    ++choosen_seg;

  if (choosen_seg == current_rep_->segments_.data.size())
    return false;

  if (choosen_seg && current_rep_->get_segment(choosen_seg)->startPTS_ > sec_in_ts)
    --choosen_seg;

  // Never seek into expired segments.....
  if (choosen_seg < current_rep_->expired_segments_)
    choosen_seg = current_rep_->expired_segments_;

  if (!preceeding && sec_in_ts > current_rep_->get_segment(choosen_seg)->startPTS_ && type_ == AdaptiveTree::VIDEO) //Assume that we have I-Frames only at segment start
    ++choosen_seg;

  const AdaptiveTree::Segment *old_seg(current_rep_->current_segment_), *newSeg(current_rep_->get_segment(choosen_seg));
  if (newSeg)
  {
    needReset = true;
    if (newSeg != old_seg)
    {
      //stop downloading chunks
      stopped_ = true;
      //wait until last reading operation stopped
      lckTree.unlock(); //writing downloadrate takes the tree lock / avoid dead-lock
      std::lock_guard<std::mutex> lck(thread_data_->mutex_dl_);
      lckTree.lock();
      stopped_ = false;
      current_rep_->current_segment_ = newSeg;
      PrepareDownload(newSeg);
      absolute_position_ = 0;
      ResetSegment();
      thread_data_->signal_dl_.notify_one();
    }
    else if (!preceeding)
    {
      absolute_position_ -= segment_read_pos_;
      segment_read_pos_ = 0;
    }
    else
      needReset = false;
    return true;
  }
  else
    current_rep_->current_segment_ = old_seg;
  return false;
}

bool AdaptiveStream::waitingForSegment(bool checkTime) const
{
  if (tree_.HasUpdateThread())
  {
    std::lock_guard<std::mutex> lckTree(tree_.GetTreeMutex());
    if (current_rep_ && (current_rep_->flags_ & AdaptiveTree::Representation::WAITFORSEGMENT) != 0)
      return !checkTime || (current_adp_->type_ != AdaptiveTree::VIDEO && current_adp_->type_ != AdaptiveTree::AUDIO) || SecondsSinceUpdate() < 1;
  }
  return false;
}

bool AdaptiveStream::select_stream(bool force, bool justInit, unsigned int repId)
{
  AdaptiveTree::Representation *new_rep(0), *min_rep(0);

  if (!repId || repId > current_adp_->repesentations_.size())
  {
    unsigned int bestScore(~0);

    for (std::vector<AdaptiveTree::Representation*>::const_iterator br(current_adp_->repesentations_.begin()), er(current_adp_->repesentations_.end()); br != er; ++br)
    {
      unsigned int score;
      if ((*br)->bandwidth_ <= bandwidth_ && (*br)->hdcpVersion_ <= hdcpVersion_
        &&  (!hdcpLimit_ || static_cast<uint32_t>((*br)->width_) * (*br)->height_ <= hdcpLimit_)
        && ((score = abs(static_cast<int>((*br)->width_ * (*br)->height_) - static_cast<int>(width_ * height_))
        + static_cast<unsigned int>(sqrt(bandwidth_ - (*br)->bandwidth_))) < bestScore))
      {
        bestScore = score;
        new_rep = (*br);
      }
      else if (!min_rep || (*br)->bandwidth_ < min_rep->bandwidth_)
        min_rep = (*br);
    }
  }
  else
    new_rep = current_adp_->repesentations_[current_adp_->repesentations_.size() - repId];

  if (!new_rep)
    new_rep = min_rep;

  if (justInit)
  {
    current_rep_ = new_rep;
    return true;
  }

  if (!force && new_rep == current_rep_)
    return false;

  uint32_t segid(current_rep_ ? current_rep_->getCurrentSegmentPos() : 0);
  if (current_rep_)
    const_cast<adaptive::AdaptiveTree::Representation*>(current_rep_)->flags_ &= ~adaptive::AdaptiveTree::Representation::ENABLED;

  current_rep_ = new_rep;
  current_rep_->current_segment_ = current_rep_->get_segment(segid);

  const_cast<adaptive::AdaptiveTree::Representation*>(current_rep_)->flags_ |= adaptive::AdaptiveTree::Representation::ENABLED;

  if (observer_)
    observer_->OnStreamChange(this);

  /* If we have indexRangeExact SegmentBase, update SegmentList from SIDX */
  if (current_rep_->indexRangeMax_)
  {
    AdaptiveTree::Representation *rep(const_cast<AdaptiveTree::Representation *>(current_rep_));
    if (!parseIndexRange())
      return false;
    rep->indexRangeMin_ = rep->indexRangeMax_ = 0;
    absolute_position_ = 0;
    stopped_ = false;
  }

  /* lets download the initialization */
  const AdaptiveTree::Segment *loadingSeg = current_rep_->get_initialization();
  if (!loadingSeg && current_rep_->flags_ & AdaptiveTree::Representation::INITIALIZATION_PREFIXED)
    loadingSeg = current_rep_->get_segment(segid);

  if (PrepareDownload(loadingSeg) && !download_segment())
  {
    stopped_ = true;
    return false;
  }

  download_url_.clear();

  return true;
}

void AdaptiveStream::info(std::ostream &s)
{
  static const char* ts[4] = { "NoType", "Video", "Audio", "Text" };
  s << ts[type_] << " representation: " << current_rep_->url_.substr(current_rep_->url_.find_last_of('/') + 1) << " bandwidth: " << current_rep_->bandwidth_ << std::endl;
}

void AdaptiveStream::stop()
{
  stopped_ = true;
  if (current_rep_)
    const_cast<adaptive::AdaptiveTree::Representation*>(current_rep_)->flags_ &= ~adaptive::AdaptiveTree::Representation::ENABLED;
  if (thread_data_)
  {
    delete thread_data_;
    thread_data_ = nullptr;
  }
};

void AdaptiveStream::clear()
{
  current_adp_ = 0;
  current_rep_ = 0;
}
