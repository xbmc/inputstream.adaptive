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
#include <string>
#include <map>

namespace adaptive
{
  class AdaptiveStream;

  class AdaptiveStreamObserver
  {
  public:
    virtual void OnStreamChange(AdaptiveStream *stream, uint32_t segment) = 0;
  };

  class AdaptiveStream
  {
  public:
    AdaptiveStream(AdaptiveTree &tree, AdaptiveTree::StreamType type);
    ~AdaptiveStream();
    void set_observer(AdaptiveStreamObserver *observer){ observer_ = observer; };
    bool prepare_stream(const AdaptiveTree::AdaptationSet *adp,
      const uint32_t width, const uint32_t height, uint32_t hdcpLimit, uint16_t hdcpVersion,
      uint32_t min_bandwidth, uint32_t max_bandwidth, unsigned int repId,
      const std::map<std::string, std::string> &media_headers);
    bool start_stream(const uint32_t seg_offset, uint16_t width, uint16_t height);
    bool select_stream(bool force = false, bool justInit = false, unsigned int repId = 0);
    void stop(){ stopped_ = true; };
    void clear();
    void info(std::ostream &s);
    unsigned int getWidth() const { return width_; };
    unsigned int getHeight() const { return height_; };
    unsigned int getBandwidth() const { return bandwidth_; };

    unsigned int get_type()const{ return type_; };

    uint32_t read(void* buffer,
      uint32_t  bytesToRead);
    uint64_t tell(){ read(0, 0);  return absolute_position_; };
    bool seek(uint64_t const pos);
    bool seek_time(double seek_seconds, double current_seconds, bool &needReset);
    AdaptiveTree::AdaptationSet const *getAdaptationSet() { return current_adp_; };
    AdaptiveTree::Representation const *getRepresentation(){ return current_rep_; };
    double get_download_speed() const { return tree_.get_download_speed(); };
    void set_download_speed(double speed) { tree_.set_download_speed(speed); };
    size_t getSegmentPos() { return current_rep_->segments_.pos(current_seg_); };
    uint64_t GetPTSOffset() { return current_seg_ ? current_seg_->startPTS_ : 0; };
  protected:
    virtual bool download(const char* url, const std::map<std::string, std::string> &mediaHeaders){ return false; };
    virtual bool parseIndexRange() { return false; };
    bool write_data(const void *buffer, size_t buffer_size);
  private:
    bool download_segment();

    AdaptiveTree &tree_;
    AdaptiveTree::StreamType type_;
    AdaptiveStreamObserver *observer_;
    // Active configuration
    const AdaptiveTree::Period *current_period_;
    const AdaptiveTree::AdaptationSet *current_adp_;
    const AdaptiveTree::Representation *current_rep_;
    const AdaptiveTree::Segment *current_seg_;
    //We assume that a single segment can build complete frames
    std::string segment_buffer_;
    std::map<std::string, std::string> media_headers_;
    std::size_t segment_read_pos_;
    uint64_t absolute_position_;

    uint16_t width_, height_;
    uint32_t bandwidth_;
    uint32_t hdcpLimit_;
    uint16_t hdcpVersion_;
    bool stopped_;
  };
};
