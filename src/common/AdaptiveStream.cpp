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
#include <math.h>

using namespace adaptive;

AdaptiveStream::AdaptiveStream(AdaptiveTree &tree, AdaptiveTree::StreamType type)
  :tree_(tree)
  , type_(type)
  , observer_(0)
  , current_period_(tree_.periods_.empty() ? 0 : tree_.periods_[0])
  , current_adp_(0)
  , current_rep_(0)
{
}

bool AdaptiveStream::download_segment()
{
  segment_buffer_.clear();
  absolute_position_ = 0;
  segment_read_pos_ = 0;

  if (!current_seg_)
    return false;

  std::string strURL;
  char rangebuf[128], *rangeHeader(0);

  if (current_rep_->flags_ & AdaptiveTree::Representation::STARTTIMETPL)
  {
    strURL = current_rep_->url_;
    sprintf(rangebuf, "%" PRIu64, tree_.base_time_ + current_seg_->range_end_);
    strURL.replace(strURL.find("{start time}"), 12, rangebuf);
  }
  else if (!(current_rep_->flags_ & AdaptiveTree::Representation::SEGMENTBASE))
  {
    if (!(current_rep_->flags_ & AdaptiveTree::Representation::TEMPLATE))
    {
      strURL = current_rep_->url_;
      sprintf(rangebuf, "bytes=%" PRIu64 "-%" PRIu64, current_seg_->range_begin_, current_seg_->range_end_);
      rangeHeader = rangebuf;
      absolute_position_ = current_seg_->range_begin_;
    }
    else if (~current_seg_->range_end_) //templated segment
    {
      std::string media = current_rep_->segtpl_.media;
      std::string::size_type lenReplace(7);
      std::string::size_type np(media.find("$Number"));
      if (np == std::string::npos)
      {
        lenReplace = 5;
        np = media.find("$Time");
      }
      np += lenReplace;

      std::string::size_type npe(media.find('$', np));

      char fmt[16];
      if (np == npe)
        strcpy(fmt, "%" PRIu64);
      else
        strcpy(fmt, media.substr(np, npe - np).c_str());

      sprintf(rangebuf, fmt, static_cast<uint64_t>(current_seg_->range_end_));
      media.replace(np - lenReplace, npe - np + lenReplace + 1, rangebuf);
      strURL = media;
    }
    else //templated initialization segment
      strURL = current_rep_->url_;
  }
  else
  {
    strURL = current_rep_->url_;
    sprintf(rangebuf, "bytes=%" PRIu64 "-%" PRIu64, current_seg_->range_begin_, current_seg_->range_end_);
    rangeHeader = rangebuf;
  }

  media_headers_["Range"] = rangeHeader ? rangeHeader : "";
  return download(strURL.c_str(), media_headers_);
}

bool AdaptiveStream::write_data(const void *buffer, size_t buffer_size)
{
  segment_buffer_ += std::string((const char *)buffer, buffer_size);
  return true;
}

bool AdaptiveStream::prepare_stream(const AdaptiveTree::AdaptationSet *adp,
  const uint32_t width, const uint32_t height,
  uint32_t min_bandwidth, uint32_t max_bandwidth, unsigned int repId, const std::map<std::string, std::string> &media_headers)
{
  width_ = type_ == AdaptiveTree::VIDEO ? width : 0;
  height_ = type_ == AdaptiveTree::VIDEO ? height : 0;

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
  if (!~seg_offset && tree_.available_time_ > 0 && current_rep_->segments_.data.size()>1)
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
    pos -= (12 * current_rep_->timescale_) / duration;
    current_seg_ = current_rep_->get_segment(pos < 0 ? 0: pos);
  }
  else
    current_seg_ = current_rep_->get_segment(~seg_offset? seg_offset:0);
  segment_buffer_.clear();
  if (!current_seg_ || !current_rep_->get_next_segment(current_seg_))
  {
    absolute_position_ = ~0;
    stopped_ = true;
  }
  else
  {
    width_ = type_ == AdaptiveTree::VIDEO ? width : 0;
    height_ = type_ == AdaptiveTree::VIDEO ? height : 0;

    absolute_position_ = current_rep_->get_next_segment(current_seg_)->range_begin_;
    stopped_ = false;
  }
  return true;
}

uint32_t AdaptiveStream::read(void* buffer, uint32_t  bytesToRead)
{
  if (stopped_)
    return 0;

  if (segment_read_pos_ >= segment_buffer_.size())
  {
    current_seg_ = current_rep_->get_next_segment(current_seg_);
    if (!download_segment() || segment_buffer_.empty())
    {
      stopped_ = true;
      return 0;
    }
  }
  if (bytesToRead)
  {
    uint32_t avail = segment_buffer_.size() - segment_read_pos_;
    if (avail > bytesToRead)
      avail = bytesToRead;
    memcpy(buffer, segment_buffer_.data() + segment_read_pos_, avail);

    segment_read_pos_ += avail;
    absolute_position_ += avail;
    return avail;
  }
  return 0;
}

bool AdaptiveStream::seek(uint64_t const pos)
{
  // we seek only in the current segment
  if (pos >= absolute_position_ - segment_read_pos_)
  {
    segment_read_pos_ = static_cast<uint32_t>(pos - (absolute_position_ - segment_read_pos_));
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

bool AdaptiveStream::seek_time(double seek_seconds, double current_seconds, bool &needReset)
{
  if (!current_rep_)
    return false;

  uint32_t choosen_seg(~0);
  
  uint64_t sec_in_ts = static_cast<uint64_t>(seek_seconds * current_rep_->timescale_);
  choosen_seg = 0; //Skip initialization
  while (choosen_seg < current_rep_->segments_.data.size() && sec_in_ts > current_rep_->get_segment(choosen_seg)->startPTS_)
    ++choosen_seg;

  if (choosen_seg == current_rep_->segments_.data.size())
    return false;

  if (choosen_seg && current_rep_->get_segment(choosen_seg)->startPTS_ > sec_in_ts)
    --choosen_seg;

  const AdaptiveTree::Segment* old_seg(current_seg_);
  if ((current_seg_ = current_rep_->get_segment(choosen_seg)))
  {
    needReset = true;
    if (current_seg_ != old_seg)
      download_segment();
    else if (seek_seconds < current_seconds)
    {
      absolute_position_ -= segment_read_pos_;
      segment_read_pos_ = 0;
    }
    else
      needReset = false;
    return true;
  }
  else
    current_seg_ = old_seg;
  return false;
}

bool AdaptiveStream::select_stream(bool force, bool justInit, unsigned int repId)
{
  const AdaptiveTree::Representation *new_rep(0), *min_rep(0);

  if (force && absolute_position_ == 0) //already selected
    return true;

  if (!repId || repId > current_adp_->repesentations_.size())
  {
    unsigned int bestScore(~0);

    for (std::vector<AdaptiveTree::Representation*>::const_iterator br(current_adp_->repesentations_.begin()), er(current_adp_->repesentations_.end()); br != er; ++br)
    {
      unsigned int score;
      if ((*br)->bandwidth_ <= bandwidth_ 
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
    new_rep = current_adp_->repesentations_[repId -1];
  
  if (!new_rep)
    new_rep = min_rep;

  if (justInit)
  {
    current_rep_ = new_rep;
    return true;
  }

  if (!force && new_rep == current_rep_)
    return false;

  uint32_t segid(current_rep_ ? current_rep_->get_segment_pos(current_seg_) : 0);

  current_rep_ = new_rep;

  if (observer_)
    observer_->OnStreamChange(this, segid);

  /* If we have indexRangeExact SegmentBase, update SegmentList from SIDX */
  if (current_rep_->indexRangeMax_)
  {
    AdaptiveTree::Representation *rep(const_cast<AdaptiveTree::Representation *>(current_rep_));
    if (!parseIndexRange())
      return false;
    rep->indexRangeMin_ = rep->indexRangeMax_ = 0;
    stopped_ = false;
  }

  /* lets download the initialization */
  if ((current_seg_ = current_rep_->get_initialization()) && !download_segment())
    return false;

  current_seg_ = current_rep_->get_segment(segid - 1);
  return true;
}

void AdaptiveStream::info(std::ostream &s)
{
  static const char* ts[4] = { "NoType", "Video", "Audio", "Text" };
  s << ts[type_] << " representation: " << current_rep_->url_.substr(current_rep_->url_.find_last_of('/') + 1) << " bandwidth: " << current_rep_->bandwidth_ << std::endl;
}

void AdaptiveStream::clear()
{
  current_adp_ = 0;
  current_rep_ = 0;
}

AdaptiveStream::~AdaptiveStream()
{
  clear();
}
