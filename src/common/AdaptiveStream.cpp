/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AdaptiveStream.h"

#ifndef INPUTSTREAM_TEST_BUILD
#include "../WebmReader.h"
#endif
#include "../oscompat.h"
#include "../utils/UrlUtils.h"
#include "../utils/log.h"
#include "Chooser.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

#include <bento4/Ap4.h>
#ifndef INPUTSTREAM_TEST_BUILD
#include <kodi/Filesystem.h>
#endif

using namespace adaptive;
using namespace UTILS;

const size_t AdaptiveStream::MAXSEGMENTBUFFER = 10;

AdaptiveStream::AdaptiveStream(AdaptiveTree& tree,
                               AdaptiveTree::AdaptationSet* adp,
                               AdaptiveTree::Representation* initialRepr,
                               const std::map<std::string, std::string>& media_headers,
                               bool play_timeshift_buffer,
                               bool choose_rep)
  : thread_data_(nullptr),
    tree_(tree),
    observer_(nullptr),
    current_period_(tree_.current_period_),
    current_adp_(adp),
    current_rep_(initialRepr),
    available_segment_buffers_(0),
    valid_segment_buffers_(0),
    media_headers_(media_headers),
    segment_read_pos_(0),
    currentPTSOffset_(0),
    absolutePTSOffset_(0),
    lastUpdated_(std::chrono::system_clock::now()),
    m_fixateInitialization(false),
    m_segmentFileOffset(0),
    play_timeshift_buffer_(play_timeshift_buffer),
    choose_rep_(choose_rep),
    rep_counter_(1),
    prev_rep_(0),
    last_rep_(0),
    assured_buffer_length_(5),
    max_buffer_length_(10)
{
  segment_buffers_.resize(MAXSEGMENTBUFFER + 1);
  current_rep_->current_segment_ = nullptr;
}

AdaptiveStream::~AdaptiveStream()
{
  stop();
  clear();
}

void AdaptiveStream::Reset()
{
  segment_read_pos_ = 0;
  currentPTSOffset_ = 0;
  absolutePTSOffset_ = 0;
}

void AdaptiveStream::ResetSegment(const AdaptiveTree::Segment* segment)
{
  segment_read_pos_ = 0;

  if (segment &&
      !(current_rep_->flags_ &
        (AdaptiveTree::Representation::SEGMENTBASE | 
         AdaptiveTree::Representation::TEMPLATE |
         AdaptiveTree::Representation::URLSEGMENTS)) &&
      current_rep_->containerType_ != AdaptiveTree::ContainerType::CONTAINERTYPE_TS)
    absolute_position_ = segment->range_begin_;
}

void AdaptiveStream::ResetActiveBuffer(bool oneValid)
{
  valid_segment_buffers_ = oneValid ? 1 : 0;
  available_segment_buffers_ = valid_segment_buffers_;
  absolute_position_ = 0;
  segment_buffers_[0].buffer.clear();
  segment_read_pos_ = 0;
}

// Make sure worker is in CV wait state.
void AdaptiveStream::StopWorker(STATE state)
{
  // stop downloading chunks
  state_ = state;
  // wait until last reading operation stopped
  // make sure download section in worker thread is done.
  std::unique_lock<std::mutex> lckrw(thread_data_->mutex_rw_);
  while (worker_processing_)
    thread_data_->signal_rw_.wait(lckrw);
  // Now the worker thread should keep the lock until it starts waiting
  // to get CV signaled - make sure we are at this point.
  std::lock_guard<std::mutex> lckdl(thread_data_->mutex_dl_);
  // Make sure that worker continues at next notify
  state_ = RUNNING;
}

bool AdaptiveStream::download_segment()
{
  if (download_url_.empty())
    return false;

  return download(download_url_, download_headers_, nullptr);
}

void AdaptiveStream::worker()
{
  std::unique_lock<std::mutex> lckdl(thread_data_->mutex_dl_);
  worker_processing_ = false;
  thread_data_->signal_dl_.notify_one();
  do
  {
    while (!thread_data_->thread_stop_ &&
           (state_ != RUNNING || valid_segment_buffers_ >= available_segment_buffers_))
      thread_data_->signal_dl_.wait(lckdl);

    if (!thread_data_->thread_stop_)
    {
      worker_processing_ = true;

      prepareNextDownload();

      // tell the main thread that we have processed prepare_download;
      thread_data_->signal_dl_.notify_one();
      lckdl.unlock();

      bool ret(download_segment());
      unsigned int retryCount(10);

      //Some streaming software offers subtitle tracks with missing fragments, usually live tv
      //When a programme is broadcasted that has subtitles, subtitles fragments are offered
      //TODO: Ensure we continue with the next segment after one retry on errors
      if (current_adp_->type_ == AdaptiveTree::SUBTITLE)
        retryCount = 1;

      while (!ret && state_ == RUNNING && retryCount-- && tree_.has_timeshift_buffer_)
      {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        LOG::LogF(LOGDEBUG, "Trying to reload segment ...");
        ret = download_segment();
      }

      lckdl.lock();

      //Signal finished download
      {
        std::lock_guard<std::mutex> lckrw(thread_data_->mutex_rw_);
        download_url_.clear();
        if (!ret)
          state_ = STOPPED;
      }
      worker_processing_ = false;

      thread_data_->signal_rw_.notify_one();
    }
  } while (!thread_data_->thread_stop_);
}

int AdaptiveStream::SecondsSinceUpdate() const
{
  const std::chrono::time_point<std::chrono::system_clock>& tPoint(
      lastUpdated_ > tree_.GetLastUpdated() ? lastUpdated_ : tree_.GetLastUpdated());
  return static_cast<int>(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - tPoint)
          .count());
}

bool AdaptiveStream::download(const std::string& url,
                              const std::map<std::string, std::string>& mediaHeaders,
                              std::string* lockfreeBuffer)
{
  kodi::vfs::CFile file;

  // open the file
  if (!file.CURLCreate(url))
    return false;
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "seekable", "0");
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "acceptencoding", "gzip, deflate");
  if (mediaHeaders.find("connection") == mediaHeaders.end())
    file.CURLAddOption(ADDON_CURL_OPTION_HEADER, "connection", "keep-alive");
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "failonerror", "false");

  for (const auto& entry : mediaHeaders)
  {
    file.CURLAddOption(ADDON_CURL_OPTION_HEADER, entry.first.c_str(), entry.second.c_str());
  }

  if (!file.CURLOpen(ADDON_READ_CHUNKED | ADDON_READ_NO_CACHE | ADDON_READ_AUDIO_VIDEO)) {
    LOG::Log(LOGERROR, "CURLOpen returned an error, download failed: %s", url.c_str());
    return false;
  }

  int returnCode = -1;
  std::string proto = file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_PROTOCOL, "");
  std::string::size_type posResponseCode = proto.find(' ');
  if (posResponseCode != std::string::npos)
    returnCode = atoi(proto.c_str() + (posResponseCode + 1));

  if (returnCode >= 400)
  {
    LOG::Log(LOGERROR, "Download failed with error %d: %s", returnCode, url.c_str());
  }
  else
  {
    // read the file
    static const size_t bufferSize{32 * 1024}; // 32 Kbyte
    std::vector<char> bufferData(bufferSize);
    ssize_t totalReadBytes{0};
    bool isEOF{false};

    while (!isEOF)
    {
      // Read the data in chunks
      ssize_t byteRead{file.Read(bufferData.data(), bufferSize)};
      if (byteRead == -1)
      {
        LOG::Log(LOGERROR, "An error occurred in the download: %s", url.c_str());
        break;
      }
      else if (byteRead == 0) // EOF or undectetable error
      {
        isEOF = true;
      }
      else
      {
        // Store the data
        if (write_data(bufferData.data(), byteRead, lockfreeBuffer))
        {
          totalReadBytes += byteRead;
        }
        else
        {
          LOG::Log(LOGDEBUG, "The download has been cancelled: %s", url.c_str());
          break;
        }
      }
    }

    if (isEOF)
    {
      if (totalReadBytes > 0)
      {
        // Get body lenght (could be gzip compressed)
        std::string contentLengthStr{
            file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, "Content-Length")};
        long contentLength{std::atol(contentLengthStr.c_str())};
        if (contentLength == 0)
          contentLength = static_cast<long>(totalReadBytes);

        double downloadSpeed{file.GetFileDownloadSpeed()};

        // Set current download speed to repr. chooser (to update average).
        // Small files are usually subtitles and their download speed are inaccurate
        // by causing side effects in the average bandwidth so we ignore them.
        static const int minSize{512 * 1024}; // 512 Kbyte
        if (contentLength > minSize)
          tree_.GetRepChooser()->SetDownloadSpeed(downloadSpeed);

        LOG::Log(LOGDEBUG, "Download finished: %s (downloaded %i byte, speed %0.2lf byte/s)",
                 url.c_str(), contentLength, downloadSpeed);

        file.Close();
        return true;
      }
      else
      {
        LOG::Log(LOGERROR, "A problem occurred in the download, no data received: %s", url.c_str());
      }
    }
  }
  file.Close();
  return false;
}

bool AdaptiveStream::parseIndexRange(AdaptiveTree::Representation* rep, const std::string& buffer)
{
#ifndef INPUTSTREAM_TEST_BUILD
  LOG::Log(LOGDEBUG, "Build segments from SIDX atom...");
  AP4_MemoryByteStream byteStream{reinterpret_cast<const AP4_Byte*>(buffer.data()),
                                  static_cast<AP4_Size>(buffer.size())};
  AdaptiveTree::AdaptationSet* adp{const_cast<AdaptiveTree::AdaptationSet*>(getAdaptationSet())};

  if (rep->containerType_ == AdaptiveTree::CONTAINERTYPE_WEBM)
  {
    if (!rep->indexRangeMin_)
      return false;

    WebmReader reader(&byteStream);
    std::vector<WebmReader::CUEPOINT> cuepoints;
    reader.GetCuePoints(cuepoints);

    if (!cuepoints.empty())
    {
      AdaptiveTree::Segment seg;

      rep->timescale_ = 1000;
      rep->SetScaling();

      rep->segments_.data.reserve(cuepoints.size());
      adp->segment_durations_.data.reserve(cuepoints.size());

      for (const WebmReader::CUEPOINT& cue : cuepoints)
      {
        seg.startPTS_ = cue.pts;
        seg.range_begin_ = cue.pos_start;
        seg.range_end_ = cue.pos_end;
        rep->segments_.data.push_back(seg);

        if (adp->segment_durations_.data.size() < rep->segments_.data.size())
          adp->segment_durations_.data.push_back(static_cast<const uint32_t>(cue.duration));
      }
      return true;
    }
  }
  else if (rep->containerType_ == AdaptiveTree::CONTAINERTYPE_MP4)
  {
    if (!rep->indexRangeMin_)
    {
      AP4_File fileStream{byteStream, AP4_DefaultAtomFactory::Instance_, true};
      AP4_Movie* movie{fileStream.GetMovie()};

      if (movie == nullptr)
      {
        LOG::Log(LOGERROR, "No MOOV in stream!");
        return false;
      }

      rep->flags_ |= AdaptiveTree::Representation::INITIALIZATION;
      rep->initialization_.range_begin_ = 0;
      AP4_Position pos;
      byteStream.Tell(pos);
      rep->initialization_.range_end_ = pos - 1;
    }

    AdaptiveTree::Segment seg;
    seg.startPTS_ = 0;
    AP4_Cardinal numSIDX{1};

    while (numSIDX > 0)
    {
      AP4_Atom* atom{nullptr};
      if (AP4_FAILED(AP4_DefaultAtomFactory::Instance_.CreateAtomFromStream(byteStream, atom)))
      {
        LOG::Log(LOGERROR, "Unable to create SIDX from IndexRange bytes");
        return false;
      }

      if (atom->GetType() == AP4_ATOM_TYPE_MOOF)
      {
        delete atom;
        break;
      }
      else if (atom->GetType() != AP4_ATOM_TYPE_SIDX)
      {
        delete atom;
        continue;
      }

      AP4_SidxAtom* sidx(AP4_DYNAMIC_CAST(AP4_SidxAtom, atom));
      const AP4_Array<AP4_SidxAtom::Reference>& refs(sidx->GetReferences());

      if (refs[0].m_ReferenceType == 1)
      {
        numSIDX = refs.ItemCount();
        delete atom;
        continue;
      }

      AP4_Position pos;
      byteStream.Tell(pos);
      seg.range_end_ = pos + rep->indexRangeMin_ + sidx->GetFirstOffset() - 1;
      rep->timescale_ = sidx->GetTimeScale();
      rep->SetScaling();

      for (AP4_Cardinal i{0}; i < refs.ItemCount(); i++)
      {
        seg.range_begin_ = seg.range_end_ + 1;
        seg.range_end_ = seg.range_begin_ + refs[i].m_ReferencedSize - 1;
        rep->segments_.data.push_back(seg);
        if (adp->segment_durations_.data.size() < rep->segments_.data.size())
          adp->segment_durations_.data.push_back(refs[i].m_SubsegmentDuration);

        seg.startPTS_ += refs[i].m_SubsegmentDuration;
      }

      delete atom;
      numSIDX--;
    }

    return true;
  }
#endif
  return false;
}

bool AdaptiveStream::write_data(const void* buffer, size_t buffer_size, std::string* lockfreeBuffer)
{
  if (lockfreeBuffer)
  {
    size_t insertPos(lockfreeBuffer->size());
    lockfreeBuffer->resize(insertPos + buffer_size);
    memcpy(&(*lockfreeBuffer)[insertPos], buffer, buffer_size);
    return true;
  }

  {
    std::lock_guard<std::mutex> lckrw(thread_data_->mutex_rw_);

    if (state_ == STOPPED)
      return false;

    // we write always into the last active segment
    std::string& segment_buffer = segment_buffers_[valid_segment_buffers_ - 1].buffer;

    size_t insertPos(segment_buffer.size());
    segment_buffer.resize(insertPos + buffer_size);
    tree_.OnDataArrived(download_segNum_, download_pssh_set_, m_iv,
                        reinterpret_cast<const uint8_t*>(buffer),
                        reinterpret_cast<uint8_t*>(&segment_buffer[0]), insertPos, buffer_size);
  }
  thread_data_->signal_rw_.notify_one();
  return true;
}

bool AdaptiveStream::start_stream()
{
  if (!current_rep_)
    return false;

  if (choose_rep_)
  {
    choose_rep_ = false;
    current_rep_ = tree_.GetRepChooser()->ChooseNextRepresentation(
        current_adp_, segment_buffers_[valid_segment_buffers_].rep);
  }

  if (!(current_rep_->flags_ & AdaptiveTree::Representation::INITIALIZED))
  {
    tree_.prepareRepresentation(current_period_, current_adp_, current_rep_,
      false);
  }

  //! @todo: the assured_buffer_duration_ and max_buffer_duration_
  //! isnt implemeted correctly and need to be reworked,
  //! these properties are intended to determine the amount of buffer
  //! customizable in seconds, but segments do not ensure that they always have
  //! a fixed duration of 1 sec moreover these properties currently works for
  //! the DASH manifest with "SegmentTemplate" tags defined only,
  //! in all other type of manifest cases always fallback on hardcoded values
  assured_buffer_length_=current_rep_->assured_buffer_duration_;
  assured_buffer_length_ = std::ceil( (assured_buffer_length_ * current_rep_->segtpl_.timescale)/ (float)current_rep_->segtpl_.duration );

  max_buffer_length_=current_rep_ ->max_buffer_duration_;
  max_buffer_length_ = std::ceil( (max_buffer_length_ * current_rep_->segtpl_.timescale)/ (float)current_rep_->segtpl_.duration );
  assured_buffer_length_  = assured_buffer_length_ <4 ? 4:assured_buffer_length_;//for incorrect settings input
  if(max_buffer_length_<=assured_buffer_length_)//for incorrect settings input
    max_buffer_length_=assured_buffer_length_+4u;
  
  segment_buffers_.resize(max_buffer_length_+ 1);//TTHR

  if (!thread_data_)
  {
    state_ = STOPPED;
    thread_data_ = new THREADDATA();
    std::unique_lock<std::mutex> lckdl(thread_data_->mutex_dl_);
    thread_data_->Start(this);
    // Wait until worker thread is waiting for input
    thread_data_->signal_dl_.wait(lckdl);
  }

  {
    // ResolveSegmentbase assumes mutex_dl locked
    std::lock_guard<std::mutex> lck(thread_data_->mutex_dl_);
    if (!ResolveSegmentBase(current_rep_, true))
    {
      state_ = STOPPED;
      return false;
    }
  }

  if (!current_rep_->current_segment_)
  {
    if (!play_timeshift_buffer_ && tree_.has_timeshift_buffer_ &&
        current_rep_->segments_.data.size() > 1 && tree_.periods_.size() == 1)
    {
      if (!last_rep_)
      {
        std::size_t pos;
        if (tree_.has_timeshift_buffer_ || tree_.available_time_ >= tree_.stream_start_)
        {
          pos = current_rep_->segments_.data.size() - 1;
        }
        else
        {
          pos = static_cast<size_t>(
              ((tree_.stream_start_ - tree_.available_time_) * current_rep_->timescale_) /
              current_rep_->duration_);
          if (pos == 0)
            pos = 1;
        }
        uint64_t duration(current_rep_->get_segment(pos)->startPTS_ -
                          current_rep_->get_segment(pos - 1)->startPTS_);
        size_t segmentPos{0};
        if (pos > (tree_.live_delay_ * current_rep_->timescale_) / duration)
        {
          segmentPos = pos - ((tree_.live_delay_ * current_rep_->timescale_) / duration);
        }
        current_rep_->current_segment_ = current_rep_->get_segment(segmentPos);
      }
      else // switching streams, align new stream segment no.
      {
        uint64_t segmentId = segment_buffers_[0].segment_number;
        if (segmentId >= current_rep_->startNumber_ + current_rep_->segments_.size())
        {
          segmentId = current_rep_->startNumber_ + current_rep_->segments_.size() - 1;
        }
        current_rep_->current_segment_ =
            current_rep_->get_segment(static_cast<size_t>(segmentId - current_rep_->startNumber_));
      }
    }
    else
      current_rep_->current_segment_ = nullptr; // start from beginning
  }

  const AdaptiveTree::Segment* next_segment =
      current_rep_->get_next_segment(current_rep_->current_segment_);

  if (!next_segment)
  {
    absolute_position_ = ~0;
    state_ = STOPPED;
    return true;
  }

  state_ = RUNNING;
  absolute_position_ = 0;

  // load the initialization segment
  const AdaptiveTree::Segment* loadingSeg = current_rep_->get_initialization();
  if (loadingSeg)
  {
    StopWorker(PAUSED);

    if (available_segment_buffers_)
      std::rotate(segment_buffers_.rend() - (available_segment_buffers_ + 1),
                  segment_buffers_.rend() - available_segment_buffers_, segment_buffers_.rend());
    segment_buffers_[0].segment.url.clear();
    ++available_segment_buffers_;

    segment_buffers_[0].segment.Copy(loadingSeg);
    segment_buffers_[0].rep = current_rep_;
    segment_buffers_[0].segment_number = ~0ULL;
    segment_buffers_[0].buffer.clear();
    segment_read_pos_ = 0;

    // Force writing the data into segment_buffers_[0]
    // Store the # of valid buffers so we can resore later
    size_t valid_segment_buffers = valid_segment_buffers_;
    valid_segment_buffers_ = 0;

    if (!prepareNextDownload() || !download_segment())
      state_ = STOPPED;

    valid_segment_buffers_ = valid_segment_buffers + 1;
  }

  if (!current_rep_->segments_.Get(0))
  {
    LOG::LogF(LOGERROR, "Segment at position 0 not found from representation id: %s",
      current_rep_->id.c_str());
    return false;
  }

  currentPTSOffset_ = (next_segment->startPTS_ * current_rep_->timescale_ext_) /
    current_rep_->timescale_int_;
  absolutePTSOffset_ = (current_rep_->segments_.Get(0)->startPTS_ * current_rep_->timescale_ext_) /
    current_rep_->timescale_int_;

  if (state_ == RUNNING)
  {
    const_cast<adaptive::AdaptiveTree::Representation*>(current_rep_)->flags_ |=
        adaptive::AdaptiveTree::Representation::ENABLED;
    return true;
  }
  return false;
}

void AdaptiveStream::ReplacePlaceholder(std::string& url, const std::string placeholder, uint64_t value)
{
  std::string::size_type lenReplace(placeholder.length());
  std::string::size_type np(url.find(placeholder));
  char rangebuf[128];

  if (np == std::string::npos)
    return;

  np += lenReplace;

  std::string::size_type npe(url.find('$', np));

  char fmt[16];
  if (np == npe)
    strcpy(fmt, "%" PRIu64);
  else
    strcpy(fmt, url.substr(np, npe - np).c_str());

  sprintf(rangebuf, fmt, value);
  url.replace(np - lenReplace, npe - np + lenReplace + 1, rangebuf);
}

bool AdaptiveStream::prepareNextDownload()
{
  // We assume, that we find the next segment to load in the next valid_segment_buffers_
  if (valid_segment_buffers_ >= available_segment_buffers_)
    return false;

  const AdaptiveTree::Representation* rep = segment_buffers_[valid_segment_buffers_].rep;
  const AdaptiveTree::Segment* seg = &segment_buffers_[valid_segment_buffers_].segment;
  // segNum == ~0U is initialization segment!
  uint64_t segNum = segment_buffers_[valid_segment_buffers_].segment_number;
  segment_buffers_[valid_segment_buffers_].buffer.clear();
  ++valid_segment_buffers_;

  return prepareDownload(rep, seg, segNum);
}

bool AdaptiveStream::prepareDownload(const AdaptiveTree::Representation* rep,
                                     const AdaptiveTree::Segment* seg,
                                     uint64_t segNum)
{
  if (!seg)
    return false;

  char rangebuf[128], *rangeHeader(0);

  if (!(rep->flags_ & AdaptiveTree::Representation::SEGMENTBASE))
  {
    if (!(rep->flags_ & AdaptiveTree::Representation::TEMPLATE))
    {
      if (rep->flags_ & AdaptiveTree::Representation::URLSEGMENTS)
      {
        if (URL::IsUrlAbsolute(seg->url))
          download_url_ = seg->url;
        else
          download_url_ = URL::Join(rep->url_, seg->url);
      }
      else
        download_url_ = rep->url_;
      if (~seg->range_begin_)
      {
        uint64_t fileOffset = ~segNum ? m_segmentFileOffset : 0;
        if (~seg->range_end_)
          sprintf(rangebuf, "bytes=%" PRIu64 "-%" PRIu64, seg->range_begin_ + fileOffset,
                  seg->range_end_ + fileOffset);
        else
          sprintf(rangebuf, "bytes=%" PRIu64 "-", seg->range_begin_ + fileOffset);
        rangeHeader = rangebuf;
      }
    }
    else if (~segNum) //templated segment
    {
      download_url_ = rep->segtpl_.media;
      ReplacePlaceholder(download_url_, "$Number", seg->range_end_);
      ReplacePlaceholder(download_url_, "$Time", seg->range_begin_);
    }
    else //templated initialization segment
      download_url_ = rep->url_;
  }
  else
  {
    if (rep->flags_ & AdaptiveTree::Representation::TEMPLATE && ~segNum)
    {
      download_url_ = rep->segtpl_.media;
      ReplacePlaceholder(download_url_, "$Number", rep->startNumber_);
      ReplacePlaceholder(download_url_, "$Time", 0);
    }
    else
      download_url_ = rep->url_;
    if (~seg->range_begin_)
    {
      uint64_t fileOffset = ~segNum ? m_segmentFileOffset : 0;
      if (~seg->range_end_)
        sprintf(rangebuf, "bytes=%" PRIu64 "-%" PRIu64, seg->range_begin_ + fileOffset,
                seg->range_end_ + fileOffset);
      else
        sprintf(rangebuf, "bytes=%" PRIu64 "-", seg->range_begin_ + fileOffset);
      rangeHeader = rangebuf;
    }
  }

  download_segNum_ = segNum;
  download_pssh_set_ = seg->pssh_set_;
  download_headers_ = media_headers_;
  if (rangeHeader)
    download_headers_["Range"] = rangeHeader;
  else
    download_headers_.erase("Range");

  download_url_ = tree_.BuildDownloadUrl(download_url_);

  return true;
}

bool AdaptiveStream::ensureSegment()
{
  if (state_ != RUNNING)
    return false;

  // We an only switch to the next segment, if the current (== segment_buffers_[0]) is finished.
  // This is the case if we have more than 1 valid segments, or worker is not processing anymore.
  if ((!worker_processing_ || valid_segment_buffers_ > 1) &&
      segment_read_pos_ >= segment_buffers_[0].buffer.size())
  {
    // wait until worker is ready for new segment
    std::unique_lock<std::mutex> lck(thread_data_->mutex_dl_);
    // lock live segment updates
    std::lock_guard<std::mutex> lckTree(tree_.GetTreeMutex());

    if (tree_.HasUpdateThread() && SecondsSinceUpdate() > 1)
    {
      tree_.RefreshSegments(current_period_, current_adp_, current_rep_, current_adp_->type_);
      lastUpdated_ = std::chrono::system_clock::now();
    }

    if (m_fixateInitialization)
      return false;

    stream_changed_ = false;
    const AdaptiveTree::Segment* nextSegment;
    last_rep_ = current_rep_;
    if (valid_segment_buffers_)
    {
      // rotate element 0 to the end
      std::rotate(segment_buffers_.begin(), segment_buffers_.begin() + 1,
                  segment_buffers_.begin() + available_segment_buffers_);
      --valid_segment_buffers_;
      --available_segment_buffers_;

      if (segment_buffers_[0].rep != current_rep_)
      {
        current_rep_->flags_ &= ~adaptive::AdaptiveTree::Representation::ENABLED;
        current_rep_ = segment_buffers_[0].rep;
        current_rep_->flags_ |= adaptive::AdaptiveTree::Representation::ENABLED;
        stream_changed_ = true;
      }
    }
    if (valid_segment_buffers_)
    {
      if (~segment_buffers_[0].segment_number)
      {
        nextSegment = current_rep_->get_segment(
            static_cast<size_t>(segment_buffers_[0].segment_number - current_rep_->startNumber_));
      }
      else
        nextSegment = nullptr;
    }
    else
      nextSegment = current_rep_->get_next_segment(current_rep_->current_segment_);

    if(prev_rep_== current_rep_)
      rep_counter_++;
    else
    {
      rep_counter_=1;
      prev_rep_=current_rep_;
    }

    if (nextSegment)
    {
      currentPTSOffset_ =
        (nextSegment->startPTS_ * current_rep_->timescale_ext_) / current_rep_->timescale_int_;

      absolutePTSOffset_ =
          (current_rep_->segments_.Get(0)->startPTS_ * current_rep_->timescale_ext_) /
          current_rep_->timescale_int_;

      current_rep_->current_segment_ = nextSegment;
      ResetSegment(nextSegment);

      if (observer_ && nextSegment != &current_rep_->initialization_ && ~nextSegment->startPTS_)
        observer_->OnSegmentChanged(this);

      size_t nextsegmentPosold = current_rep_->get_segment_pos(nextSegment);
      size_t nextsegno = current_rep_->getSegmentNumber(nextSegment);
      AdaptiveTree::Representation* newRep{nullptr};
      bool lastSeg =
          (current_period_ != tree_.periods_.back() &&
           nextsegmentPosold + available_segment_buffers_ == current_rep_->segments_.size() - 1);
      
      if (segment_buffers_[0].segment_number == ~0ULL || valid_segment_buffers_ == 0 ||
          current_adp_->type_ != AdaptiveTree::VIDEO)
      {
        newRep = current_rep_;
      }
      else if (lastSeg) // Don't change reps on last segment of period, use the rep of preceeding seg
      {
        newRep = segment_buffers_[valid_segment_buffers_ - 1].rep;
      }
      else
      {
        // Defer until we have some free buffer
        if (available_segment_buffers_ < max_buffer_length_) {
          newRep = tree_.GetRepChooser()->ChooseNextRepresentation(
              current_adp_, segment_buffers_[available_segment_buffers_ - 1].rep);
        }
        else
          newRep = current_rep_;
      }

      // Make sure, new representation has segments!
      ResolveSegmentBase(newRep, false); // For DASH

      if (tree_.SecondsSinceRepUpdate(newRep) > 1)
      {
        tree_.prepareRepresentation(
          current_period_, current_adp_, newRep, tree_.has_timeshift_buffer_);
      }

      size_t nextsegmentPos = nextsegno - newRep->startNumber_;
      if (nextsegmentPos + available_segment_buffers_ >= newRep->segments_.size())
      {
        nextsegmentPos = newRep->segments_.size() - available_segment_buffers_;
      }
      for (size_t updPos(available_segment_buffers_); updPos < max_buffer_length_; ++updPos)
      {
        const AdaptiveTree::Segment* futureSegment = newRep->get_segment(nextsegmentPos + updPos);

        if (futureSegment)
        {
          segment_buffers_[updPos].segment.Copy(futureSegment);
          segment_buffers_[updPos].segment_number = newRep->startNumber_ + nextsegmentPos + updPos;
          segment_buffers_[updPos].rep = newRep;
          ++available_segment_buffers_;
        }
        else
          break;
      }

      thread_data_->signal_dl_.notify_one();
      // Make sure that we have at least one segment filling
      // Otherwise we lead into a deadlock because first condition is false.
      if (!valid_segment_buffers_)
        thread_data_->signal_dl_.wait(lck);

      if (stream_changed_)
      {
        if (observer_)
          observer_->OnStreamChange(this);
        return false;
      }
    }
    else if (tree_.HasUpdateThread() && current_period_ == tree_.periods_.back())
    {
      current_rep_->flags_ |= AdaptiveTree::Representation::WAITFORSEGMENT;
      LOG::LogF(LOGDEBUG, "Begin WaitForSegment stream %s", current_rep_->id.c_str());
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      return false;
    }
    else
    {
      state_ = STOPPED;
      return false;
    }
  }
  return true;
}


uint32_t AdaptiveStream::read(void* buffer, uint32_t bytesToRead)
{
  if (state_ == STOPPED)
    return false;

  std::unique_lock<std::mutex> lckrw(thread_data_->mutex_rw_);

NEXTSEGMENT:
  if (ensureSegment() && bytesToRead)
  {
    while (true)
    {
      uint32_t avail = segment_buffers_[0].buffer.size() - segment_read_pos_;
      if (avail < bytesToRead && worker_processing_)
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
        memcpy(buffer, segment_buffers_[0].buffer.data() + (segment_read_pos_ - avail), avail);
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
  if (state_ == STOPPED)
    return false;

  std::unique_lock<std::mutex> lckrw(thread_data_->mutex_rw_);

  // we seek only in the current segment
  if (state_ != STOPPED && pos >= absolute_position_ - segment_read_pos_)
  {
    segment_read_pos_ = static_cast<uint32_t>(pos - (absolute_position_ - segment_read_pos_));

    while (segment_read_pos_ > segment_buffers_[0].buffer.size() && worker_processing_)
      thread_data_->signal_rw_.wait(lckrw);

    if (segment_read_pos_ > segment_buffers_[0].buffer.size())
    {
      segment_read_pos_ = static_cast<uint32_t>(segment_buffers_[0].buffer.size());
      return false;
    }
    absolute_position_ = pos;
    return true;
  }
  return false;
}

bool AdaptiveStream::retrieveCurrentSegmentBufferSize(size_t& size)
{
  if (state_ == STOPPED)
    return false;

  std::unique_lock<std::mutex> lckrw(thread_data_->mutex_rw_);

  while (true)
  {
    if (worker_processing_)
    {
      thread_data_->signal_rw_.wait(lckrw);
      continue;
    }
    size = segment_buffers_[0].buffer.size();
    return true;
  }

  return false;
}

uint64_t AdaptiveStream::getMaxTimeMs()
{
  if (current_rep_->flags_ & AdaptiveTree::Representation::SUBTITLESTREAM)
    return 0;

  if (current_rep_->segments_.empty())
    return 0;

  uint64_t duration{0};
  if (current_rep_->segments_.size() > 1)
  {
    duration = current_rep_->segments_.Get(current_rep_->segments_.size() - 1)->startPTS_ -
               current_rep_->segments_.Get(current_rep_->segments_.size() - 2)->startPTS_;
  }

  uint64_t timeExt =
      ((current_rep_->segments_.Get(current_rep_->segments_.size() - 1)->startPTS_ + duration) *
       current_rep_->timescale_ext_) /
      current_rep_->timescale_int_;

  return (timeExt - absolutePTSOffset_) / 1000;
}

bool AdaptiveStream::seek_time(double seek_seconds, bool preceeding, bool& needReset)
{
  if (!current_rep_)
    return false;

  if (state_ == STOPPED)
    // For subtitles which come in one file we should return true!
    return current_rep_->segments_.empty();

  if (current_rep_->flags_ & AdaptiveTree::Representation::SUBTITLESTREAM)
    return true;

  std::unique_lock<std::mutex> lckTree(tree_.GetTreeMutex());

  uint32_t choosen_seg(~0);

  uint64_t sec_in_ts = static_cast<uint64_t>(seek_seconds * current_rep_->timescale_);
  choosen_seg = 0; //Skip initialization
  while (choosen_seg < current_rep_->segments_.data.size() &&
         sec_in_ts > current_rep_->get_segment(choosen_seg)->startPTS_)
    ++choosen_seg;

  if (choosen_seg == current_rep_->segments_.data.size())
  {
    if (!current_rep_->segments_.Get(0))
    {
      LOG::LogF(LOGERROR, "Segment at position 0 not found from representation id: %s",
        current_rep_->id.c_str());
      return false;
    }

    if (sec_in_ts < current_rep_->segments_.Get(0)->startPTS_ + current_rep_->duration_)
      --choosen_seg;
    else
      return false;
  }

  if (choosen_seg && current_rep_->get_segment(choosen_seg)->startPTS_ > sec_in_ts)
    --choosen_seg;

  // Never seek into expired segments.....
  if (choosen_seg < current_rep_->expired_segments_)
    choosen_seg = current_rep_->expired_segments_;

  if (!preceeding && sec_in_ts > current_rep_->get_segment(choosen_seg)->startPTS_ &&
      current_adp_->type_ ==
          AdaptiveTree::VIDEO) //Assume that we have I-Frames only at segment start
    ++choosen_seg;

  const AdaptiveTree::Segment *old_seg(current_rep_->current_segment_),
      *newSeg(current_rep_->get_segment(choosen_seg));
  if (newSeg)
  {
    needReset = true;
    if (newSeg != old_seg)
    {
      StopWorker(STOPPED);
      // EnsureSegment loads always the next segment, so go back 1
      current_rep_->current_segment_ =
          current_rep_->get_segment(current_rep_->get_segment_pos(newSeg) - 1);
      // TODO: if new segment is already prefetched, don't ResetActiveBuffer;
      ResetActiveBuffer(false);
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
      return !checkTime ||
             (current_adp_->type_ != AdaptiveTree::VIDEO &&
              current_adp_->type_ != AdaptiveTree::AUDIO) ||
             SecondsSinceUpdate() < 1;
  }
  return false;
}

void AdaptiveStream::FixateInitialization(bool on)
{
  m_fixateInitialization = on && current_rep_->get_initialization() != nullptr;
}

bool AdaptiveStream::ResolveSegmentBase(AdaptiveTree::Representation* rep, bool stopWorker)
{
  /* If we have indexRangeExact SegmentBase, update SegmentList from SIDX */
  if (rep->flags_ & AdaptiveTree::Representation::SEGMENTBASE)
  {
    // We assume mutex_dl is locked so we can safely call prepare_download
    AdaptiveTree::Segment seg;
    unsigned int segNum = ~0U;
    if (rep->indexRangeMin_ || !(rep->get_initialization()))
    {
      seg.range_begin_ = rep->indexRangeMin_;
      seg.range_end_ = rep->indexRangeMax_;
      seg.startPTS_ = ~0ULL;
      seg.pssh_set_ = 0;
      segNum = 0; // It's no an initialization segment
    }
    else if (rep->get_initialization())
      seg = *rep->get_initialization();
    else
      return false;

    std::string sidxBuffer;
    if (prepareDownload(rep, &seg, segNum) &&
        download(download_url_, download_headers_, &sidxBuffer) && parseIndexRange(rep, sidxBuffer))
    {
      const_cast<AdaptiveTree::Representation*>(rep)->flags_ &=
          ~AdaptiveTree::Representation::SEGMENTBASE;
    }
    else
      return false;
  }
  return true;
}

void AdaptiveStream::info(std::ostream& s)
{
  static const char* ts[4] = {"NoType", "Video", "Audio", "Text"};
  s << ts[current_adp_->type_]
    << " representation: " << current_rep_->url_.substr(current_rep_->url_.find_last_of('/') + 1)
    << " bandwidth: " << current_rep_->bandwidth_ << std::endl;
}

void AdaptiveStream::stop()
{
  if (current_rep_) 
  {
    const_cast<adaptive::AdaptiveTree::Representation*>(current_rep_)->flags_ &=
        ~adaptive::AdaptiveTree::Representation::ENABLED;
  }

  if (thread_data_)
  {
    StopWorker(STOPPED);
    delete thread_data_;
    thread_data_ = nullptr;
  }
};

void AdaptiveStream::clear()
{
  current_adp_ = 0;
  current_rep_ = 0;
}
