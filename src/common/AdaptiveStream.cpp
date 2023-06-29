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
#include "../utils/CurlUtils.h"
#include "../utils/UrlUtils.h"
#include "../utils/log.h"
#include "Chooser.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

#include <bento4/Ap4.h>
#include "kodi/tools/StringUtils.h"

using namespace adaptive;
using namespace std::chrono_literals;
using namespace kodi::tools;
using namespace PLAYLIST;
using namespace UTILS;

uint32_t AdaptiveStream::globalClsId = 0;

AdaptiveStream::AdaptiveStream(AdaptiveTree& tree,
                               PLAYLIST::CAdaptationSet* adp,
                               PLAYLIST::CRepresentation* initialRepr,
                               const UTILS::PROPERTIES::KodiProperties& kodiProps,
                               bool choose_rep)
  : thread_data_(nullptr),
    tree_(tree),
    observer_(nullptr),
    current_period_(tree_.m_currentPeriod),
    current_adp_(adp),
    current_rep_(initialRepr),
    available_segment_buffers_(0),
    valid_segment_buffers_(0),
    m_streamParams(kodiProps.m_streamParams),
    m_streamHeaders(kodiProps.m_streamHeaders),
    segment_read_pos_(0),
    currentPTSOffset_(0),
    absolutePTSOffset_(0),
    lastUpdated_(std::chrono::system_clock::now()),
    m_fixateInitialization(false),
    m_segmentFileOffset(0),
    play_timeshift_buffer_(kodiProps.m_playTimeshiftBuffer),
    choose_rep_(choose_rep),
    rep_counter_(1),
    prev_rep_(0),
    last_rep_(0),
    assured_buffer_length_(5),
    max_buffer_length_(10)
{
  current_rep_->current_segment_ = nullptr;

  // Set the class id for debug purpose
  clsId = globalClsId++;
  LOG::Log(LOGDEBUG,
           "Created AdaptiveStream [AS-%u] with adaptation set ID: \"%s\", stream type: %s", clsId,
           adp->GetId().data(), StreamTypeToString(adp->GetStreamType()).data());
}

AdaptiveStream::~AdaptiveStream()
{
  Stop();
  DisposeWorker();
  clear();
  DeallocateSegmentBuffers();
}

void AdaptiveStream::Reset()
{
  segment_read_pos_ = 0;
  currentPTSOffset_ = 0;
  absolutePTSOffset_ = 0;
}

void adaptive::AdaptiveStream::AllocateSegmentBuffers(size_t size)
{
  size++;

  while (size-- > 0)
  {
    segment_buffers_.emplace_back(new SEGMENTBUFFER());
  }
}

void adaptive::AdaptiveStream::DeallocateSegmentBuffers()
{
  for (auto itSegBuf = segment_buffers_.begin(); itSegBuf != segment_buffers_.end();)
  {
    delete *itSegBuf;
    itSegBuf = segment_buffers_.erase(itSegBuf);
  }
}

bool adaptive::AdaptiveStream::Download(const DownloadInfo& downloadInfo, std::string& data)
{
  return DownloadImpl(downloadInfo, &data);
}

bool adaptive::AdaptiveStream::DownloadSegment(const DownloadInfo& downloadInfo)
{
  if (!downloadInfo.m_segmentBuffer)
  {
    LOG::LogF(LOGERROR, "[AS-%u] Download failed, no segment buffer", clsId);
    return false;
  }
  return DownloadImpl(downloadInfo, nullptr);
}

bool adaptive::AdaptiveStream::DownloadImpl(const DownloadInfo& downloadInfo,
                                            std::string* downloadData)
{
  if (downloadInfo.m_url.empty())
    return false;

  std::string url = downloadInfo.m_url;

  // Merge additional headers to the predefined one
  std::map<std::string, std::string> headers = m_streamHeaders;
  headers.insert(downloadInfo.m_addHeaders.begin(), downloadInfo.m_addHeaders.end());

  // Append stream parameters, only if not already provided
  if (url.find('?') == std::string::npos)
    URL::AppendParameters(url, m_streamParams);

  CURL::CUrl curl{url};
  curl.AddHeaders(headers);

  int statusCode = curl.Open(true);

  if (statusCode == -1)
    LOG::Log(LOGERROR, "[AS-%u] Download failed, internal error: %s", clsId, url.c_str());
  else if (statusCode >= 400)
    LOG::Log(LOGERROR, "[AS-%u] Download failed, HTTP error %d: %s", clsId, statusCode,
             url.c_str());
  else // Start the download
  {
    CURL::ReadStatus downloadStatus = CURL::ReadStatus::CHUNK_READ;
    bool isChunked = curl.IsChunked();

    while (downloadStatus == CURL::ReadStatus::CHUNK_READ)
    {
      std::vector<char> bufferData(CURL::BUFFER_SIZE_32);
      size_t bytesRead{0};

      downloadStatus = curl.ReadChunk(bufferData.data(), CURL::BUFFER_SIZE_32, bytesRead);

      if (downloadStatus == CURL::ReadStatus::CHUNK_READ)
      {
        if (downloadData) // Write the data in to the string
        {
          downloadData->append(bufferData.data(), bytesRead);
        }
        else // Write the data to the segment buffer
        {
          // We only set lastChunk to true in the case of non-chunked transfers, the
          // current structure does not allow for knowing the file has finished for
          // chunked transfers here - IsEOF() will return true while doing chunked transfers
          bool isLastChunk = !isChunked && curl.IsEOF();
          {
            std::lock_guard<std::mutex> lckrw(thread_data_->mutex_rw_);

            // The status can be changed after waiting for the lock_guard e.g. video seek/stop
            if (state_ == STOPPED)
              break;

            std::string& segmentBuffer = downloadInfo.m_segmentBuffer->buffer;

            tree_.OnDataArrived(downloadInfo.m_segmentBuffer->segment_number,
                                downloadInfo.m_segmentBuffer->segment.pssh_set_, m_decrypterIv,
                                bufferData.data(), bytesRead, segmentBuffer, segmentBuffer.size(),
                                isLastChunk);
          }
          thread_data_->signal_rw_.notify_all();
        }
      }
    }

    if (downloadStatus == CURL::ReadStatus::ERROR)
    {
      LOG::Log(LOGERROR, "[AS-%u] Download failed, cannot read chunk: %s", clsId, url.c_str());
    }
    else if (downloadStatus == CURL::ReadStatus::CHUNK_READ)
    {
      // Chunk reading operations have been stopped
      LOG::Log(LOGDEBUG, "[AS-%u] Download cancelled: %s", clsId, url.c_str());
    }
    else if (downloadStatus == CURL::ReadStatus::IS_EOF)
    {
      if (curl.GetTotalByteRead() == 0)
      {
        LOG::Log(LOGERROR, "[AS-%u] Download failed, no data: %s", clsId, url.c_str());
        return false;
      }

      size_t totalBytesRead = curl.GetTotalByteRead();
      double downloadSpeed = curl.GetDownloadSpeed();

      // Set current download speed to repr. chooser (to update average).
      // Small files are usually subtitles and their download speed are inaccurate
      // by causing side effects in the average bandwidth so we ignore them.
      static const size_t minSize{512 * 1024}; // 512 Kbyte
      if (totalBytesRead > minSize)
        tree_.GetRepChooser()->SetDownloadSpeed(downloadSpeed);

      LOG::Log(LOGDEBUG, "[AS-%u] Download finished: %s (downloaded %zu byte, speed %0.2lf byte/s)",
               clsId, url.c_str(), totalBytesRead, downloadSpeed);
      return true;
    }
  }
  return false;
}

bool AdaptiveStream::PrepareNextDownload(DownloadInfo& downloadInfo)
{
  // We assume, that we find the next segment to load in the next valid_segment_buffers_
  if (valid_segment_buffers_ >= available_segment_buffers_)
    return false;

  SEGMENTBUFFER* segBuffer = segment_buffers_[valid_segment_buffers_];
  ++valid_segment_buffers_;

  // Clear existing data
  segBuffer->buffer.clear();
  downloadInfo.m_segmentBuffer = segBuffer;

  return PrepareDownload(segBuffer->rep, segBuffer->segment, downloadInfo);
}

bool AdaptiveStream::PrepareDownload(const PLAYLIST::CRepresentation* rep,
                                     const PLAYLIST::CSegment& seg,
                                     DownloadInfo& downloadInfo)
{
  std::string rangeHeader;
  std::string streamUrl;

  if (rep->HasSegmentTemplate())
  {
    auto segTpl = rep->GetSegmentTemplate();

    if (seg.IsInitialization()) // Templated initialization segment
    {
      streamUrl = segTpl->FormatUrl(segTpl->GetInitialization(), rep->GetId().data(),
                                    rep->GetBandwidth(), rep->GetStartNumber(), 0);
    }
    else // Templated media segment
    {
      streamUrl = segTpl->FormatUrl(segTpl->GetMedia(), rep->GetId().data(), rep->GetBandwidth(),
                                    seg.m_number, seg.m_time);
    }
  }
  else
  {
    if (seg.url.empty())
      streamUrl = rep->GetBaseUrl();
    else
      streamUrl = seg.url;
  }

  if (URL::IsUrlRelative(streamUrl))
    streamUrl = URL::Join(rep->GetBaseUrl(), streamUrl);

  if (seg.range_begin_ != NO_VALUE)
  {
    uint64_t fileOffset = seg.IsInitialization() ? 0 : m_segmentFileOffset;
    if (seg.range_end_ != NO_VALUE)
    {
      rangeHeader = StringUtils::Format("bytes=%llu-%llu", seg.range_begin_ + fileOffset,
                                        seg.range_end_ + fileOffset);
    }
    else
    {
      rangeHeader = StringUtils::Format("bytes=%llu-", seg.range_begin_ + fileOffset);
    }
  }

  if (!rangeHeader.empty())
    downloadInfo.m_addHeaders["Range"] = rangeHeader;

  downloadInfo.m_url = tree_.BuildDownloadUrl(streamUrl);
  return true;
}

void AdaptiveStream::ResetSegment(const PLAYLIST::CSegment* segment)
{
  segment_read_pos_ = 0;

  if (segment)
  {
    if (!current_rep_->HasSegmentBase() && !current_rep_->HasSegmentTemplate() &&
        !current_rep_->HasSegmentsUrl() &&
        current_rep_->GetContainerType() != ContainerType::TS)
    {
      absolute_position_ = segment->range_begin_;
    }
  }
}

void AdaptiveStream::ResetActiveBuffer(bool oneValid)
{
  valid_segment_buffers_ = oneValid ? 1 : 0;
  available_segment_buffers_ = valid_segment_buffers_;
  absolute_position_ = 0;
  segment_buffers_[0]->buffer.clear();
  segment_read_pos_ = 0;
}

bool AdaptiveStream::StopWorker(STATE state)
{
  // stop downloading chunks
  state_ = state;
  // wait until last reading operation stopped
  // make sure download section in worker thread is done.
  std::unique_lock<std::mutex> lckrw(thread_data_->mutex_rw_);
  while (worker_processing_)
  {
    // While we are waiting the state of worker may be changed
    thread_data_->signal_rw_.wait(lckrw);
  }

  // Now if the state set is PAUSED/STOPPED the worker thread should keep the lock to mutex_dl_
  // and wait for a signal to condition varibale "signal_dl_.wait",
  // if state will be not changed to RUNNING next downloads will be not performed.

  // Check if the worker state is changed by other situations
  // e.g. stop playback or download cancelled
  // that invalidated our status
  return state_ == state;
}

void adaptive::AdaptiveStream::WaitWorker()
{
  // If the worker is in PAUSED/STOPPED state
  // we wait here until condition variable "signal_dl_.wait" is executed,
  // after that the worker will be waiting for a signal to unlock "signal_dl_.wait" (blocking thread)
  std::lock_guard<std::mutex> lckdl(thread_data_->mutex_dl_);
  // Make sure that worker continue the loop (avoid signal_dl_.wait block again the thread)
  // and allow new downloads
  state_ = RUNNING;
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
    {
      thread_data_->signal_dl_.wait(lckdl);
    }

    if (!thread_data_->thread_stop_)
    {
      worker_processing_ = true;

      DownloadInfo downloadInfo;
      if (!PrepareNextDownload(downloadInfo))
      {
        worker_processing_ = false;
        continue;
      }

      // tell the main thread that we have processed prepare_download;
      thread_data_->signal_dl_.notify_one();
      lckdl.unlock();

      //! @todo: for live content we should calculate max attempts and sleep timing
      //! based on segment duration / playlist updates timing
      size_t maxAttempts = tree_.IsLive() ? 10 : 6;
      std::chrono::milliseconds msSleep = tree_.IsLive() ? 1000ms : 500ms;

      //! @todo: Some streaming software offers subtitle tracks with missing fragments, usually live tv
      //! When a programme is broadcasted that has subtitles, subtitles fragments are offered,
      //! Ensure we continue with the next segment after one retry on errors
      if (current_adp_->GetStreamType() == StreamType::SUBTITLE && tree_.IsLive())
        maxAttempts = 2;

      size_t downloadAttempts = 1;
      bool isSegmentDownloaded = false;

      // Download errors may occur e.g. due to unstable connection, server overloading, ...
      // then we try downloading the segment more times before aborting playback
      while (state_ != STOPPED)
      {
        isSegmentDownloaded = DownloadSegment(downloadInfo);
        if (isSegmentDownloaded || downloadAttempts == maxAttempts || state_ == STOPPED)
          break;

        //! @todo: forcing thread sleep block the thread also while the state_ / thread_stop_ change values
        //! we have to interrupt the sleep when it happens
        std::this_thread::sleep_for(msSleep);
        downloadAttempts++;
        LOG::Log(LOGWARNING, "[AS-%u] Segment download failed, attempt %zu...", clsId, downloadAttempts);
      }

      lckdl.lock();

      if (!isSegmentDownloaded)
      {
        std::lock_guard<std::mutex> lckrw(thread_data_->mutex_rw_);
        // Download cancelled or cannot download the file
        state_ = STOPPED;
      }

      // Signal finished download
      worker_processing_ = false;
      thread_data_->signal_rw_.notify_all();
    }
  } while (!thread_data_->thread_stop_);

  worker_processing_ = false;
  lckdl.unlock();
}

int AdaptiveStream::SecondsSinceUpdate() const
{
  const std::chrono::time_point<std::chrono::system_clock>& tPoint(
      lastUpdated_ > tree_.GetLastUpdated() ? lastUpdated_ : tree_.GetLastUpdated());
  return static_cast<int>(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - tPoint)
          .count());
}

bool AdaptiveStream::parseIndexRange(PLAYLIST::CRepresentation* rep, const std::string& buffer)
{
#ifndef INPUTSTREAM_TEST_BUILD
  LOG::Log(LOGDEBUG, "[AS-%u] Build segments from SIDX atom...", clsId);
  AP4_MemoryByteStream byteStream{reinterpret_cast<const AP4_Byte*>(buffer.data()),
                                  static_cast<AP4_Size>(buffer.size())};

  CAdaptationSet* adpSet = getAdaptationSet();

  if (rep->GetContainerType() == ContainerType::WEBM)
  {
    if (rep->GetSegmentBase()->GetIndexRangeBegin() == 0)
      return false;

    WebmReader reader(&byteStream);
    std::vector<WebmReader::CUEPOINT> cuepoints;
    reader.GetCuePoints(cuepoints);

    if (!cuepoints.empty())
    {
      CSegment seg;

      rep->SetTimescale(1000);
      rep->SetScaling();

      rep->SegmentTimeline().GetData().reserve(cuepoints.size());
      adpSet->SegmentTimelineDuration().GetData().reserve(cuepoints.size());

      for (const WebmReader::CUEPOINT& cue : cuepoints)
      {
        seg.startPTS_ = cue.pts;
        seg.range_begin_ = cue.pos_start;
        seg.range_end_ = cue.pos_end;
        rep->SegmentTimeline().GetData().emplace_back(seg);

        if (adpSet->SegmentTimelineDuration().GetSize() < rep->SegmentTimeline().GetSize())
        {
          adpSet->SegmentTimelineDuration().GetData().emplace_back(
              static_cast<uint32_t>(cue.duration));
        }
      }
      return true;
    }
  }
  else if (rep->GetContainerType() == ContainerType::MP4)
  {
    if (rep->GetSegmentBase()->GetIndexRangeBegin() == 0)
    {
      AP4_File fileStream{byteStream, AP4_DefaultAtomFactory::Instance_, true};
      AP4_Movie* movie{fileStream.GetMovie()};

      if (movie == nullptr)
      {
        LOG::Log(LOGERROR, "[AS-%u] No MOOV in stream!", clsId);
        return false;
      }

      if (!rep->HasInitSegment())
      {
        LOG::LogF(LOGERROR, "[AS-%u] Representation has no init segment", clsId);
        return false;      
      }
      rep->GetInitSegment()->range_begin_ = 0;
      AP4_Position pos;
      byteStream.Tell(pos);
      rep->GetInitSegment()->range_end_ = pos - 1;
    }

    CSegment seg;
    seg.startPTS_ = 0;
    AP4_Cardinal numSIDX{1};
    uint64_t reprDuration{0};

    while (numSIDX > 0)
    {
      AP4_Atom* atom{nullptr};
      if (AP4_FAILED(AP4_DefaultAtomFactory::Instance_.CreateAtomFromStream(byteStream, atom)))
      {
        LOG::Log(LOGERROR, "[AS-%u] Unable to create SIDX from IndexRange bytes", clsId);
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
      seg.range_end_ =
          pos + rep->GetSegmentBase()->GetIndexRangeBegin() + sidx->GetFirstOffset() - 1;
      rep->SetTimescale(sidx->GetTimeScale());
      rep->SetScaling();

      for (AP4_Cardinal i{0}; i < refs.ItemCount(); i++)
      {
        seg.range_begin_ = seg.range_end_ + 1;
        seg.range_end_ = seg.range_begin_ + refs[i].m_ReferencedSize - 1;
        rep->SegmentTimeline().GetData().emplace_back(seg);

        if (adpSet->SegmentTimelineDuration().GetSize() < rep->SegmentTimeline().GetSize())
        {
          adpSet->SegmentTimelineDuration().GetData().emplace_back(refs[i].m_SubsegmentDuration);
        }

        seg.startPTS_ += refs[i].m_SubsegmentDuration;
        reprDuration += refs[i].m_SubsegmentDuration;
      }

      delete atom;
      numSIDX--;
    }

    rep->SetDuration(reprDuration);

    return true;
  }
#endif
  return false;
}

bool AdaptiveStream::start_stream()
{
  if (!current_rep_)
    return false;

  if (choose_rep_)
  {
    choose_rep_ = false;
    current_rep_ = tree_.GetRepChooser()->GetRepresentation(current_adp_);
  }

  if (!current_rep_->IsPrepared())
  {
    tree_.prepareRepresentation(current_period_, current_adp_, current_rep_, false);
  }

  //! @todo: the assured_buffer_duration_ and max_buffer_duration_
  //! isnt implemeted correctly and need to be reworked,
  //! these properties are intended to determine the amount of buffer
  //! customizable in seconds, but segments do not ensure that they always have
  //! a fixed duration of 1 sec moreover these properties currently works for
  //! the DASH manifest with "SegmentTemplate" tags defined only,
  //! in all other type of manifest cases always fallback on hardcoded values
  assured_buffer_length_ = current_rep_->assured_buffer_duration_;
  max_buffer_length_ = current_rep_->max_buffer_duration_;
  if (current_rep_->HasSegmentTemplate())
  {
    const auto& segTemplate = current_rep_->GetSegmentTemplate();
    assured_buffer_length_ = std::ceil((assured_buffer_length_ * segTemplate->GetTimescale()) /
                                       static_cast<float>(segTemplate->GetDuration()));
    max_buffer_length_ = std::ceil((max_buffer_length_ * segTemplate->GetTimescale()) /
                                   static_cast<float>(segTemplate->GetDuration()));
  }
  assured_buffer_length_  = assured_buffer_length_ <4 ? 4:assured_buffer_length_;//for incorrect settings input
  if(max_buffer_length_<=assured_buffer_length_)//for incorrect settings input
    max_buffer_length_=assured_buffer_length_+4u;

  AllocateSegmentBuffers(max_buffer_length_);

  if (!thread_data_)
  {
    state_ = STOPPED;
    thread_data_ = new THREADDATA();
    std::unique_lock<std::mutex> lckdl(thread_data_->mutex_dl_);
    thread_data_->Start(this);
    // Wait until worker thread is waiting for input
    thread_data_->signal_dl_.wait(lckdl);
  }

  if (current_rep_->SegmentTimeline().IsEmpty() && current_rep_->HasSegmentBase())
  {
    // ResolveSegmentbase assumes mutex_dl locked
    std::lock_guard<std::mutex> lck(thread_data_->mutex_dl_);
    if (!ResolveSegmentBase(current_rep_))
    {
      state_ = STOPPED;
      return false;
    }
  }

  if (!current_rep_->current_segment_)
  {
    if (!play_timeshift_buffer_ && tree_.IsLive() &&
        current_rep_->SegmentTimeline().GetSize() > 1 && tree_.m_periods.size() == 1)
    {
      if (!last_rep_)
      {
        std::size_t pos;
        if (tree_.IsLive() || tree_.available_time_ >= tree_.stream_start_)
        {
          pos = current_rep_->SegmentTimeline().GetSize() - 1;
        }
        else
        {
          pos = static_cast<size_t>(
              ((tree_.stream_start_ - tree_.available_time_) * current_rep_->GetTimescale()) /
              current_rep_->GetDuration());
          if (pos == 0)
            pos = 1;
        }
        uint64_t duration(current_rep_->get_segment(pos)->startPTS_ -
                          current_rep_->get_segment(pos - 1)->startPTS_);
        size_t segmentPos{0};
        size_t segPosDelay =
            static_cast<size_t>((tree_.m_liveDelay * current_rep_->GetTimescale()) / duration);

        if (pos > segPosDelay)
        {
          segmentPos = pos - segPosDelay;
        }
        current_rep_->current_segment_ = current_rep_->get_segment(segmentPos);
      }
      else // switching streams, align new stream segment no.
      {
        uint64_t segmentId = segment_buffers_[0]->segment_number;
        if (segmentId >= current_rep_->GetStartNumber() + current_rep_->SegmentTimeline().GetSize())
        {
          segmentId =
              current_rep_->GetStartNumber() + current_rep_->SegmentTimeline().GetSize() - 1;
        }
        current_rep_->current_segment_ =
            current_rep_->get_segment(static_cast<size_t>(segmentId - current_rep_->GetStartNumber()));
      }
    }
    else
      current_rep_->current_segment_ = nullptr; // start from beginning
  }

  const CSegment* next_segment =
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
  if (current_rep_->HasInitSegment())
  {
    StopWorker(PAUSED);
    WaitWorker();

    if (available_segment_buffers_)
      std::rotate(segment_buffers_.rend() - (available_segment_buffers_ + 1),
                  segment_buffers_.rend() - available_segment_buffers_, segment_buffers_.rend());
    ++available_segment_buffers_;

    segment_buffers_[0]->segment = *current_rep_->GetInitSegment();
    segment_buffers_[0]->rep = current_rep_;
    segment_buffers_[0]->buffer.clear();
    segment_read_pos_ = 0;

    // Force writing the data into segment_buffers_[0]
    // Store the # of valid buffers so we can resore later
    size_t valid_segment_buffers = valid_segment_buffers_;
    valid_segment_buffers_ = 0;

    DownloadInfo downloadInfo;
    if (!PrepareNextDownload(downloadInfo) || !DownloadSegment(downloadInfo))
      state_ = STOPPED;

    valid_segment_buffers_ = valid_segment_buffers + 1;
  }

  if (!current_rep_->SegmentTimeline().Get(0))
  {
    LOG::LogF(LOGERROR, "[AS-%u] Segment at position 0 not found from representation id: %s",
              clsId, current_rep_->GetId().data());
    return false;
  }

  currentPTSOffset_ = (next_segment->startPTS_ * current_rep_->timescale_ext_) /
    current_rep_->timescale_int_;
  absolutePTSOffset_ =
      (current_rep_->SegmentTimeline().Get(0)->startPTS_ * current_rep_->timescale_ext_) /
    current_rep_->timescale_int_;

  if (state_ == RUNNING)
  {
    current_rep_->SetIsEnabled(true);
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

bool AdaptiveStream::ensureSegment()
{
  if (state_ != RUNNING)
    return false;

  // We an only switch to the next segment, if the current (== segment_buffers_[0]) is finished.
  // This is the case if we have more than 1 valid segments, or worker is not processing anymore.
  if ((!worker_processing_ || valid_segment_buffers_ > 1) &&
      segment_read_pos_ >= segment_buffers_[0]->buffer.size())
  {
    // wait until worker is ready for new segment
    std::unique_lock<std::mutex> lck(thread_data_->mutex_dl_);
    // lock live segment updates
    std::lock_guard<adaptive::AdaptiveTree::TreeUpdateThread> lckUpdTree(tree_.GetTreeUpdMutex());

    if (tree_.HasManifestUpdatesSegs() && SecondsSinceUpdate() > 1)
    {
      tree_.RefreshSegments(current_period_, current_adp_, current_rep_, current_adp_->GetStreamType());
      lastUpdated_ = std::chrono::system_clock::now();
    }

    if (m_fixateInitialization)
      return false;

    stream_changed_ = false;
    CSegment* nextSegment{nullptr};
    last_rep_ = current_rep_;
    if (valid_segment_buffers_)
    {
      // rotate element 0 to the end
      std::rotate(segment_buffers_.begin(), segment_buffers_.begin() + 1,
                  segment_buffers_.begin() + available_segment_buffers_);
      --valid_segment_buffers_;
      --available_segment_buffers_;

      if (segment_buffers_[0]->rep != current_rep_)
      {
        current_rep_->SetIsEnabled(false);
        current_rep_ = segment_buffers_[0]->rep;
        current_rep_->SetIsEnabled(true);
        stream_changed_ = true;
      }
    }
    if (valid_segment_buffers_)
    {
      if (!segment_buffers_[0]->segment.IsInitialization())
      {
        nextSegment = current_rep_->get_segment(static_cast<size_t>(
            segment_buffers_[0]->segment_number - current_rep_->GetStartNumber()));
      }
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
          (current_rep_->SegmentTimeline().Get(0)->startPTS_ * current_rep_->timescale_ext_) /
          current_rep_->timescale_int_;

      current_rep_->current_segment_ = nextSegment;
      ResetSegment(nextSegment);

      if (observer_ && !nextSegment->IsInitialization() &&
          nextSegment->startPTS_ != NO_PTS_VALUE)
      {
        observer_->OnSegmentChanged(this);
      }

      size_t nextsegmentPosold = current_rep_->get_segment_pos(nextSegment);
      uint64_t nextsegno = current_rep_->getSegmentNumber(nextSegment);
      CRepresentation* newRep{nullptr};

      bool lastSeg{false};
      if (current_period_ != tree_.m_periods.back().get())
      {
        if (nextsegmentPosold + available_segment_buffers_ ==
            current_rep_->SegmentTimeline().GetSize() - 1)
        {
          lastSeg = true;
        }
      }
      
      if (segment_buffers_[0]->segment.IsInitialization() ||
          valid_segment_buffers_ == 0 ||
          current_adp_->GetStreamType() != StreamType::VIDEO)
      {
        newRep = current_rep_;
      }
      else if (lastSeg) // Don't change reps on last segment of period, use the rep of preceeding seg
      {
        newRep = segment_buffers_[valid_segment_buffers_ - 1]->rep;
      }
      else
      {
        // Defer until we have some free buffer
        if (available_segment_buffers_ < max_buffer_length_) {
          newRep = tree_.GetRepChooser()->GetNextRepresentation(
              current_adp_, segment_buffers_[available_segment_buffers_ - 1]->rep);
        }
        else
          newRep = current_rep_;
      }

      // If the representation has been changed, segments may have to be generated (DASH)
      if (newRep->SegmentTimeline().IsEmpty() && newRep->HasSegmentBase())
        ResolveSegmentBase(newRep);

      if (!newRep->IsPrepared() && tree_.SecondsSinceRepUpdate(newRep) > 1)
      {
        tree_.prepareRepresentation(
          current_period_, current_adp_, newRep, tree_.IsLive());
      }

      size_t nextsegmentPos = static_cast<size_t>(nextsegno - newRep->GetStartNumber());
      if (nextsegmentPos + available_segment_buffers_ >= newRep->SegmentTimeline().GetSize())
      {
        nextsegmentPos = newRep->SegmentTimeline().GetSize() - available_segment_buffers_;
      }
      for (size_t updPos(available_segment_buffers_); updPos < max_buffer_length_; ++updPos)
      {
        const CSegment* futureSegment = newRep->get_segment(nextsegmentPos + updPos);

        if (futureSegment)
        {
          segment_buffers_[updPos]->segment = *futureSegment;
          segment_buffers_[updPos]->segment_number =
              newRep->GetStartNumber() + nextsegmentPos + updPos;
          segment_buffers_[updPos]->rep = newRep;
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
    else if ((tree_.HasManifestUpdates() || tree_.HasManifestUpdatesSegs()) &&
             current_period_ == tree_.m_periods.back().get())
    {
      if (!current_rep_->IsWaitForSegment())
      {
        current_rep_->SetIsWaitForSegment(true);
        LOG::LogF(LOGDEBUG, "[AS-%u] Begin WaitForSegment stream %s", clsId, current_rep_->GetId().data());
      }
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
      uint32_t avail = segment_buffers_[0]->buffer.size() - segment_read_pos_;

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
        memcpy(buffer, segment_buffers_[0]->buffer.data() + (segment_read_pos_ - avail), avail);
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

    while (segment_read_pos_ > segment_buffers_[0]->buffer.size() && worker_processing_)
      thread_data_->signal_rw_.wait(lckrw);

    if (segment_read_pos_ > segment_buffers_[0]->buffer.size())
    {
      segment_read_pos_ = static_cast<uint32_t>(segment_buffers_[0]->buffer.size());
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

  if (!StopWorker(PAUSED))
    return false;

  size = segment_buffers_[0]->buffer.size();
  WaitWorker();
  return true;
}

uint64_t AdaptiveStream::getMaxTimeMs()
{
  if (current_rep_->IsSubtitleFileStream())
    return 0;

  if (current_rep_->SegmentTimeline().IsEmpty())
    return 0;

  uint64_t duration{0};
  if (current_rep_->SegmentTimeline().GetSize() > 1)
  {
    duration =
        current_rep_->SegmentTimeline().Get(current_rep_->SegmentTimeline().GetSize() - 1)->startPTS_ -
        current_rep_->SegmentTimeline().Get(current_rep_->SegmentTimeline().GetSize() - 2)->startPTS_;
  }

  uint64_t timeExt = ((current_rep_->SegmentTimeline()
                           .Get(current_rep_->SegmentTimeline().GetSize() - 1)
                           ->startPTS_ +
                       duration) *
                      current_rep_->timescale_ext_) /
                     current_rep_->timescale_int_;

  return (timeExt - absolutePTSOffset_) / 1000;
}

void AdaptiveStream::ResetCurrentSegment(const PLAYLIST::CSegment* newSegment)
{
  StopWorker(STOPPED);
  WaitWorker();
  // EnsureSegment loads always the next segment, so go back 1
  current_rep_->current_segment_ =
    current_rep_->get_segment(current_rep_->get_segment_pos(newSegment) - 1);
  // TODO: if new segment is already prefetched, don't ResetActiveBuffer;
  ResetActiveBuffer(false);
}

int adaptive::AdaptiveStream::GetTrackType() const
{
  if (!current_adp_)
  {
    LOG::LogF(LOGERROR, "[AS-%u] Failed get track type, current adaptation set is nullptr.", clsId);
    return AP4_Track::TYPE_UNKNOWN;
  }

  switch (current_adp_->GetStreamType())
  {
    case StreamType::VIDEO:
      return AP4_Track::TYPE_VIDEO;
    case StreamType::AUDIO:
      return AP4_Track::TYPE_AUDIO;
    case StreamType::SUBTITLE:
      return AP4_Track::TYPE_SUBTITLES;
    default:
      LOG::LogF(LOGERROR, "[AS-%u] Stream type \"%i\" not mapped to AP4_Track::Type",
                clsId, static_cast<int>(current_adp_->GetStreamType()));
      break;
  }
  return AP4_Track::TYPE_UNKNOWN;
}

PLAYLIST::StreamType adaptive::AdaptiveStream::GetStreamType() const
{
  if (!current_adp_)
  {
    LOG::LogF(LOGERROR, "[AS-%u] Failed get stream type, current adaptation set is nullptr.", clsId);
    return StreamType::NOTYPE;
  }
  return current_adp_->GetStreamType();
}

bool AdaptiveStream::seek_time(double seek_seconds, bool preceeding, bool& needReset)
{
  if (!current_rep_)
    return false;

  if (current_rep_->IsSubtitleFileStream())
    return true;

  std::lock_guard<adaptive::AdaptiveTree::TreeUpdateThread> lckUpdTree(tree_.GetTreeUpdMutex());

  uint64_t sec_in_ts = static_cast<uint64_t>(seek_seconds * current_rep_->GetTimescale());

  //Skip initialization
  size_t choosen_seg{0};

  while (choosen_seg < current_rep_->SegmentTimeline().GetSize() &&
         sec_in_ts > current_rep_->get_segment(choosen_seg)->startPTS_)
  {
    ++choosen_seg;
  }

  if (choosen_seg == current_rep_->SegmentTimeline().GetSize())
  {
    if (!current_rep_->SegmentTimeline().Get(0))
    {
      LOG::LogF(LOGERROR, "[AS-%u] Segment at position 0 not found from representation id: %s",
                clsId, current_rep_->GetId().data());
      return false;
    }

    if (sec_in_ts < current_rep_->SegmentTimeline().Get(0)->startPTS_ + current_rep_->GetDuration())
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
      current_adp_->GetStreamType() == StreamType::VIDEO)
  {
    //Assume that we have I-Frames only at segment start
    ++choosen_seg;
  }

  CSegment* old_seg = current_rep_->current_segment_;
  const CSegment* newSeg = current_rep_->get_segment(choosen_seg);

  if (newSeg)
  {
    needReset = true;
    if (newSeg != old_seg)
    {
      ResetCurrentSegment(newSeg);
    }
    else if (!preceeding)
    {
      // restart stream if it has 'finished', e.g in the case of subtitles
      // where there may be a few or only one segment for the period and 
      // the stream is now in EOS state (all data already passed to Kodi)
      if (state_ == STOPPED)
      {
        ResetCurrentSegment(newSeg);
      }
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
  if ((tree_.HasManifestUpdates() || tree_.HasManifestUpdatesSegs()) && state_ == RUNNING)
  {
    std::lock_guard<adaptive::AdaptiveTree::TreeUpdateThread> lckUpdTree(tree_.GetTreeUpdMutex());

    if (current_rep_ && current_rep_->IsWaitForSegment())
    {
      return !checkTime ||
             (current_adp_->GetStreamType() != StreamType::VIDEO &&
              current_adp_->GetStreamType() != StreamType::AUDIO) ||
             SecondsSinceUpdate() < 1;
    }
  }
  return false;
}

void AdaptiveStream::FixateInitialization(bool on)
{
  m_fixateInitialization = on && current_rep_->HasInitSegment();
}

bool AdaptiveStream::ResolveSegmentBase(PLAYLIST::CRepresentation* rep)
{
  // Get the byte ranges to download the index segment to generate media segments from SIDX atom

  auto& segBase = rep->GetSegmentBase();
  CSegment seg;

  if (!rep->HasInitSegment() && segBase->GetIndexRangeBegin() == 0 &&
      segBase->GetIndexRangeEnd() > 0)
  {
    seg.SetIsInitialization(true);
    seg.range_end_ = segBase->GetIndexRangeEnd();
    // Initialization segment will be set to representation by ParseIndexRange
  }
  else if (segBase->GetIndexRangeBegin() > 0 || !rep->HasInitSegment())
  {
    // It's no an initialization segment
    seg.range_begin_ = segBase->GetIndexRangeBegin();
    seg.range_end_ = segBase->GetIndexRangeEnd();
  }
  else if (rep->HasInitSegment())
  {
    seg = *rep->GetInitSegment();
  }
  else
    return false;

  std::string sidxBuffer;
  DownloadInfo downloadInfo;
  // We assume mutex_dl is locked so we can safely call prepare_download
  if (PrepareDownload(rep, seg, downloadInfo) && Download(downloadInfo, sidxBuffer) &&
      parseIndexRange(rep, sidxBuffer))
  {
    return true;
  }

  return false;
}

void AdaptiveStream::Stop()
{
  if (current_rep_) 
  {
    current_rep_->SetIsEnabled(false);
  }

  if (thread_data_)
  {
    thread_data_->Stop();
    StopWorker(STOPPED);
  }
}

void AdaptiveStream::clear()
{
  current_adp_ = 0;
  current_rep_ = 0;
}

void adaptive::AdaptiveStream::DisposeWorker()
{
  if (thread_data_)
  {
    if (worker_processing_)
    {
      LOG::LogF(LOGERROR, "[AS-%u] Cannot delete worker thread, download is in progress.", clsId);
      return;
    }
    if (!thread_data_->thread_stop_)
    {
      LOG::LogF(LOGERROR, "[AS-%u] Cannot delete worker thread, loop is still running.", clsId);
      return;
    }
    delete thread_data_;
    thread_data_ = nullptr;
  }
}
