/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AdaptiveStream.h"

#include "AdaptiveTree.h"
#ifndef INPUTSTREAM_TEST_BUILD
#include "demuxers/WebmReader.h"
#endif
#include "Chooser.h"
#include "CompKodiProps.h"
#include "SrvBroker.h"
#include "utils/StringUtils.h"
#include "utils/CurlUtils.h"
#include "utils/UrlUtils.h"
#include "utils/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

#include <bento4/Ap4.h>

#include <kodi/addon-instance/inputstream/TimingConstants.h>

using namespace adaptive;
using namespace std::chrono_literals;
using namespace ADP;
using namespace PLAYLIST;
using namespace UTILS;

uint32_t adaptive::AdaptiveStream::globalClsId = 0;

adaptive::AdaptiveStream::AdaptiveStream(AdaptiveTree* tree,
                                         PLAYLIST::CAdaptationSet* adp,
                                         PLAYLIST::CRepresentation* initialRepr)
  : m_tree(tree),
    current_period_(m_tree->m_currentPeriod),
    current_adp_(adp),
    current_rep_(initialRepr),
    lastUpdated_(std::chrono::system_clock::now())
{
  auto& kodiProps = CSrvBroker::GetKodiProps();
  m_streamParams = kodiProps.GetStreamParams();
  m_streamHeaders = kodiProps.GetStreamHeaders();

  current_rep_->current_segment_.reset();

  // Set the class id for debug purpose
  clsId = globalClsId++;
  LOG::Log(LOGDEBUG,
           "Created AdaptiveStream [AS-%u] with adaptation set ID: \"%s\", stream type: %s", clsId,
           adp->GetId().c_str(), StreamTypeToString(adp->GetStreamType()).c_str());
}

adaptive::AdaptiveStream::~AdaptiveStream()
{
  Stop();
  Dispose();
  clear();
}

void adaptive::AdaptiveStream::Reset()
{
  segment_read_pos_ = 0;
  currentPTSOffset_ = 0;
  absolutePTSOffset_ = 0;
}

bool adaptive::AdaptiveStream::Download(const DownloadInfo& downloadInfo,
                                        std::vector<uint8_t>& data)
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
                                            std::vector<uint8_t>* downloadData)
{
  if (downloadInfo.m_url.empty())
    return false;

  std::string url = downloadInfo.m_url;

  // Merge additional headers to the predefined one
  std::map<std::string, std::string> headers = m_streamHeaders;
  headers.insert(downloadInfo.m_addHeaders.begin(), downloadInfo.m_addHeaders.end());

  // Append stream parameters
  URL::AppendParameters(url, m_streamParams);

  CURL::CUrl curl{url};
  curl.AddHeaders(headers);

  int statusCode = curl.Open();

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
      std::vector<uint8_t> bufferData(CURL::BUFFER_SIZE_32);
      size_t bytesRead{0};

      downloadStatus = curl.ReadChunk(bufferData.data(), CURL::BUFFER_SIZE_32, bytesRead);

      if (downloadStatus == CURL::ReadStatus::CHUNK_READ)
      {
        if (downloadData) // Write the data in to the string
        {
          downloadData->insert(downloadData->end(), bufferData.begin(), bufferData.end());
        }
        else // Write the data to the segment buffer
        {
          // We only set lastChunk to true in the case of non-chunked transfers, the
          // current structure does not allow for knowing the file has finished for
          // chunked transfers here - IsEOF() will return true while doing chunked transfers
          bool isLastChunk = !isChunked && curl.IsEOF();

          // The status can be changed after waiting for the lock_guard e.g. video seek/stop
          if (thread_data_->State() == THREADDATA::ThState::STOPPED)
            break;

          std::vector<uint8_t> bufferOutput;

          m_tree->OnDataArrived(downloadInfo.m_segmentBuffer->segment.m_number,
                                downloadInfo.m_segmentBuffer->segment.AESKeyInfo(), m_decrypterIv,
                                bufferData.data(), bytesRead, bufferOutput,
                                downloadInfo.m_segmentBuffer->BufferSize(), isLastChunk);

          downloadInfo.m_segmentBuffer->AppendBuffer(bufferOutput);
          thread_data_->cvRW.notify_all();
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
      }
      else
      {
        size_t totalBytesRead = curl.GetTotalByteRead();
        double downloadSpeed = curl.GetDownloadSpeed();

        // Set current download speed to repr. chooser (to update average).
        // Small files are usually subtitles and their download speed are inaccurate
        // by causing side effects in the average bandwidth so we ignore them.
        static const size_t minSize{512 * 1024}; // 512 Kbyte
        if (totalBytesRead > minSize)
          m_tree->GetRepChooser()->SetDownloadSpeed(downloadSpeed);

        LOG::Log(LOGDEBUG,
                 "[AS-%u] Download finished: %s (downloaded %zu byte, speed %0.2lf byte/s)", clsId,
                 url.c_str(), totalBytesRead, downloadSpeed);
        return true;
      }
    }
  }

  return false;
}

bool adaptive::AdaptiveStream::PrepareNextDownload(DownloadInfo& downloadInfo)
{
  SegmentBuffer* segBuffer = m_segBuffers.GetNextDownload();

  if (!segBuffer)
    return false;

  downloadInfo.m_segmentBuffer = segBuffer;

  return PrepareDownload(segBuffer->rep, segBuffer->segment, downloadInfo);
}

bool adaptive::AdaptiveStream::PrepareDownload(const PLAYLIST::CRepresentation* rep,
                                               const PLAYLIST::CSegment& seg,
                                               DownloadInfo& downloadInfo)
{
  std::string streamUrl;

  if (rep->HasSegmentTemplate())
  {
    auto segTpl = rep->GetSegmentTemplate();

    if (seg.IsInitialization()) // Templated initialization segment
    {
      streamUrl = segTpl->FormatUrl(segTpl->GetInitialization(), rep->GetId(),
                                    rep->GetBandwidth(), rep->GetStartNumber(), 0);
    }
    else // Templated media segment
    {
      streamUrl = segTpl->FormatUrl(segTpl->GetMedia(), rep->GetId(), rep->GetBandwidth(),
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

  if (seg.HasByteRange())
  {
    std::string rangeHeader;
    uint64_t fileOffset = seg.IsInitialization() ? 0 : m_segmentFileOffset;

    if (seg.range_end_ != NO_VALUE)
    {
      rangeHeader = STRING::Format("bytes=%llu-%llu", seg.range_begin_ + fileOffset,
                                   seg.range_end_ + fileOffset);
    }
    else
    {
      rangeHeader = STRING::Format("bytes=%llu-", seg.range_begin_ + fileOffset);
    }

    downloadInfo.m_addHeaders["Range"] = rangeHeader;
  }

  downloadInfo.m_url = streamUrl;
  return true;
}

void adaptive::AdaptiveStream::ResetSegment(const PLAYLIST::CSegment& segment)
{
  segment_read_pos_ = 0;

  if (segment.HasByteRange() && !current_rep_->HasSegmentBase() &&
      !current_rep_->HasSegmentTemplate() && current_rep_->GetContainerType() != ContainerType::TS)
  {
    absolute_position_ = segment.range_begin_;
  }
}

bool adaptive::AdaptiveStream::AlignBufferToSegment(const PLAYLIST::CSegment& segment)
{
  if (m_segBuffers.ContainsSegment(segment))
  {
    thread_data_->PauseDownloads();
    // Check if there is a download in progress and if its PTS
    // is older than the requested segment, if so immediately stop the download
    std::unique_lock<std::mutex> lckWorker(thread_data_->mutexWorker, std::try_to_lock);
    if (!lckWorker.owns_lock())
    {
      // No lock, there is a possible download in progress
      {
        std::unique_lock<std::mutex> lckrw(thread_data_->mutexRW);
        // Note: doesnt matter if the download is finished right now and GetNextDownload return next buffer
        SegmentBuffer* currDownload = m_segBuffers.GetNextDownload();

        if (currDownload && currDownload->State() == BufferState::DOWNLOADING)
        {
          const uint64_t downloadStartPTS = currDownload->segment.startPTS_;
          const uint64_t targetStartPTS = segment.startPTS_;

          if (downloadStartPTS < targetStartPTS)
            thread_data_->StopDownloads();
        }
      }
      lckWorker.lock();
    }

    // Delete all segments older than the requested one from the buffer
    while (!m_segBuffers.IsEmpty())
    {
      if (!segment.IsSame(m_segBuffers.Front().segment))
        m_segBuffers.PopFront();
      else
        break;
    }
  }
  else
  {
    thread_data_->StopDownloads();
    {
      std::lock_guard<std::mutex> lckWorker(thread_data_->mutexWorker);
      m_segBuffers.Reset();
    }
  }

  thread_data_->StartDownloads();
  return !m_segBuffers.IsEmpty();
}

void adaptive::AdaptiveStream::worker()
{
  do
  {
    // Check the thread state to wait in case of PAUSE or STOP
    {
      std::unique_lock<std::mutex> lckWorker(thread_data_->mutexWorker);
      thread_data_->cvState.wait(lckWorker,
                                 [&]
                                 {
                                   return thread_data_->State() == THREADDATA::ThState::RUNNING ||
                                          thread_data_->IsThreadExit();
                                 });

      if (thread_data_->IsThreadExit())
        break;
    }

    // Check if there is a segment to download, or wait for a notification
    m_segBuffers.WaitForSegment();

    if (!thread_data_->IsThreadExit())
    {
      std::lock_guard<std::mutex> lckWorker(thread_data_->mutexWorker);

      DownloadInfo downloadInfo;
      if (!PrepareNextDownload(downloadInfo))
      {
        if (downloadInfo.m_segmentBuffer)
        {
          {
            std::lock_guard<std::mutex> lckrw(thread_data_->mutexRW);
            downloadInfo.m_segmentBuffer->ChangeState(BufferState::INVALID);
            m_segBuffers.NotifyDownloadCompleted();
          }
          thread_data_->cvRW.notify_all();
        }
        continue;
      }

      downloadInfo.m_segmentBuffer->ChangeState(BufferState::DOWNLOADING);

      //! @todo: for live content we should calculate max attempts and sleep timing
      //! based on segment duration / playlist updates timing
      size_t maxAttempts = m_tree->IsLive() ? 6 : 3;
      std::chrono::milliseconds msSleep = m_tree->IsLive() ? 1000ms : 500ms;

      size_t downloadAttempts = 1;
      bool isSegmentDownloaded = false;

      // Download errors may occur e.g. due to unstable connection, server overloading, ...
      // then we try downloading the segment more times before aborting playback
      while (thread_data_->State() != THREADDATA::ThState::STOPPED)
      {
        isSegmentDownloaded = DownloadSegment(downloadInfo);
        if (isSegmentDownloaded || downloadAttempts == maxAttempts ||
            thread_data_->State() == THREADDATA::ThState::STOPPED)
          break;

        //! @todo: forcing thread sleep block the thread also while the state_ / thread_stop_ change values
        //! we have to interrupt the sleep when it happens
        std::this_thread::sleep_for(msSleep);
        downloadAttempts++;
        LOG::Log(LOGWARNING, "[AS-%u] Segment download failed, attempt %zu...", clsId,
                 downloadAttempts);
      }

      m_segBuffers.NotifyDownloadCompleted();
      downloadInfo.m_segmentBuffer->ChangeState(isSegmentDownloaded ? BufferState::DOWNLOADED
                                                                    : BufferState::INVALID);

      // Stop the playback if the data cant be downloaded
      // is not the case for subtitles where in the case of missing files they can be ignored
      if (!isSegmentDownloaded && current_adp_->GetStreamType() != StreamType::SUBTITLE)
      {
        // Download cancelled or cannot download the file
        thread_data_->StopDownloads();
      }

      thread_data_->cvRW.notify_all();
    }

  } while (!thread_data_->IsThreadExit());
}

int adaptive::AdaptiveStream::SecondsSinceUpdate() const
{
  const std::chrono::time_point<std::chrono::system_clock>& tPoint(
      lastUpdated_ > m_tree->GetLastUpdated() ? lastUpdated_ : m_tree->GetLastUpdated());
  return static_cast<int>(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - tPoint)
          .count());
}

void adaptive::AdaptiveStream::OnTFRFatom(uint64_t ts, uint64_t duration, uint32_t mediaTimescale)
{
  m_tree->InsertLiveFragment(current_adp_, current_rep_, ts, duration, mediaTimescale);
}

bool adaptive::AdaptiveStream::IsRequiredCreateMovieAtom()
{
  return m_tree->GetTreeType() == TreeType::SMOOTH_STREAMING;
}

bool adaptive::AdaptiveStream::parseIndexRange(PLAYLIST::CRepresentation* rep,
                                               const std::vector<uint8_t>& buffer)
{
#ifndef INPUTSTREAM_TEST_BUILD
  LOG::Log(LOGDEBUG, "[AS-%u] Build segments from SIDX atom...", clsId);
  AP4_MemoryByteStream byteStream{reinterpret_cast<const AP4_Byte*>(buffer.data()),
                                  static_cast<AP4_Size>(buffer.size())};

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

      for (const WebmReader::CUEPOINT& cue : cuepoints)
      {
        seg.startPTS_ = cue.pts;
        seg.m_endPts = seg.startPTS_ + cue.duration;
        seg.m_time = cue.pts;
        seg.range_begin_ = cue.pos_start;
        seg.range_end_ = cue.pos_end;
        rep->Timeline().Add(seg);
      }

      return true;
    }
  }
  else if (rep->GetContainerType() == ContainerType::MP4)
  {
    uint64_t boxSize{0};
    uint64_t initRangeEnd{NO_VALUE};
    // Note: if the init segment is set, means that we have downloaded data starting from the IndexRangeBegin offset
    // so we need to include the data size not downloaded to the begin range of first segment
    if (rep->HasSegmentBase() && rep->HasInitSegment())
    {
      boxSize = rep->GetSegmentBase()->GetIndexRangeBegin();
      initRangeEnd = boxSize - 1;
    }

    bool isMoovFound{false};
    AP4_Cardinal sidxCount{1};

    CSegment seg;
    seg.startPTS_ = 0;
    seg.m_time = 0;

    // Iterate each atom in the stream
    AP4_DefaultAtomFactory atomFactory;
    AP4_Atom* atom{nullptr};
    while (AP4_SUCCEEDED(atomFactory.CreateAtomFromStream(byteStream, atom)))
    {
      AP4_Position streamPos{0}; // Current stream position (offset where ends the current box)
      byteStream.Tell(streamPos);

      if (atom->GetType() == AP4_ATOM_TYPE_MOOV)
      {
        isMoovFound = true;
        initRangeEnd = streamPos - 1;
        delete atom;
      }
      else if (atom->GetType() == AP4_ATOM_TYPE_MOOF || atom->GetType() == AP4_ATOM_TYPE_MDAT)
      {
        // Stop iteration because media segments are started
        delete atom;
        break;
      }
      else if (atom->GetType() == AP4_ATOM_TYPE_SIDX && sidxCount > 0)
      {
        AP4_SidxAtom* sidx = AP4_DYNAMIC_CAST(AP4_SidxAtom, atom);
        const AP4_Array<AP4_SidxAtom::Reference>& refs = sidx->GetReferences();

        if (refs[0].m_ReferenceType == 1) // type 1 ref to a sidx box, type 0 ref to a moof box
        {
          sidxCount = refs.ItemCount();
          delete atom;
          continue;
        }

        rep->SetTimescale(sidx->GetTimeScale());
        rep->SetScaling();

        seg.range_end_ = streamPos + boxSize + sidx->GetFirstOffset() - 1;

        // Compute current period boundaries in milliseconds to filter SIDX refs
        uint64_t periodStartMs = current_period_->GetStart();
        if (periodStartMs == NO_VALUE)
          periodStartMs = 0;

        uint64_t periodEndMs = NO_PTS_VALUE;
        if (current_period_->GetTlDuration() != 0)
        {
          const uint64_t periodDurMs = current_period_->GetTlDuration() * 1000 / current_period_->GetTimescale();
          periodEndMs = periodStartMs + periodDurMs;
        }

        // Iterate each SIDX reference to create the segments, and filter them based on the current period boundaries
        for (AP4_Cardinal i{0}; i < refs.ItemCount(); i++)
        {
          seg.range_begin_ = seg.range_end_ + 1;
          seg.range_end_ = seg.range_begin_ + refs[i].m_ReferencedSize - 1;
          seg.m_endPts = seg.startPTS_ + refs[i].m_SubsegmentDuration;

          // Convert segment PTS to milliseconds using representation timescale
          uint64_t segStartMs{0};
          uint64_t segEndMs{0};
          if (rep->GetTimescale() != 0)
          {
            segStartMs = seg.startPTS_ * 1000 / rep->GetTimescale();
            segEndMs = seg.m_endPts * 1000 / rep->GetTimescale();
          }

          // Determine if this segment belongs to the current period
          bool inCurrentPeriod = true;
          if (segStartMs < periodStartMs)
            inCurrentPeriod = false;
          if (periodEndMs != NO_PTS_VALUE && segStartMs >= periodEndMs)
            inCurrentPeriod = false;

          if (inCurrentPeriod)
            rep->Timeline().Add(seg);

          seg.startPTS_ += refs[i].m_SubsegmentDuration;
          seg.m_time += refs[i].m_SubsegmentDuration;
        }

        sidxCount--;
        delete atom;
      }
    }

    if (!rep->HasInitSegment())
    {
      if (!isMoovFound)
      {
        LOG::LogF(LOGERROR, "[AS-%u] Cannot create init segment, missing MOOV atom in stream",
                  clsId);
        return false;
      }
      if (initRangeEnd == NO_VALUE)
      {
        LOG::LogF(LOGERROR, "[AS-%u] Cannot create init segment, cannot determinate range end",
                  clsId);
        return false;
      }
      // Create the initialization segment
      CSegment initSeg;
      initSeg.SetIsInitialization(true);
      initSeg.range_begin_ = 0;
      initSeg.range_end_ = initRangeEnd;
      rep->SetInitSegment(initSeg);
    }

    // Update period timeline duration
    if (current_adp_->GetStreamType() == StreamType::VIDEO || current_adp_->GetStreamType() == StreamType::AUDIO)
    {
      if (rep->GetTimescale() == 0)
      {
        LOG::LogF(LOGERROR, "Cannot calculate timeline duration, missing timescale attribute");
      }
      else
      {
        const uint64_t tlDuration =
            rep->Timeline().GetDuration() * current_period_->GetTimescale() / rep->GetTimescale();
        current_period_->SetTlDuration(tlDuration);
      }
    }

    return true;
  }
#endif
  return false;
}

bool adaptive::AdaptiveStream::start_stream()
{
  if (!current_rep_ || current_rep_->IsSubtitleFileStream())
    return false;

  //! @todo: its hardcoded the buffer size to 4 segments
  //! originally introduced to ensure smooth 4K playback
  //! this is a temporary solution until we implement a better adaptive buffer management
  //! its important to note that set the buffer too high can cause RAM to become saturated resulting in a crash
  m_segBuffers.SetMaxSize(4);

  if (!thread_data_)
  {
    thread_data_ = new THREADDATA();
    thread_data_->Initialize(this);
  }

  if (current_rep_->Timeline().IsEmpty())
  {
    if (!GenerateSidxSegments(current_rep_))
    {
      return false;
    }
  }

  if (!current_rep_->current_segment_.has_value())
  {
    if (m_startEvent == EVENT_TYPE::STREAM_START && m_tree->IsLive() &&
        !m_tree->IsChangingPeriod() && !CSrvBroker::GetKodiProps().IsPlayTimeshift() &&
        !current_rep_->Timeline().IsEmpty())
    {
      uint64_t totalDurSecs{0};
      const uint64_t liveDelaySecs = m_tree->m_liveDelay;
      const uint32_t timescale = current_rep_->GetTimescale();

      //! @todo: This code does not consider that the live delay could cause the startup segment to be selected
      //! in the previous period when the current period has too few segments
      //! more likely live delay management should be moved just after manifest parsing and before period init
      auto timelineItRend = current_rep_->Timeline().rend();

      for (auto itSeg = current_rep_->Timeline().rbegin(); itSeg != timelineItRend;
           ++itSeg)
      {
        // Implicit rounding down because managing PTS milliseconds negatively affects segment selection
        totalDurSecs += (itSeg->m_endPts - itSeg->startPTS_) / timescale;
        // Dont use >= since if live delay is equal to the segment duration
        // we may fall too close to the live edge to get new segments from manifest update
        if (totalDurSecs > liveDelaySecs)
        {
          if (itSeg != timelineItRend)
          {
            // GetNextSegment used below requires the previous one, then advance
            ++itSeg;
            if (itSeg != timelineItRend)
              current_rep_->current_segment_ = *itSeg;
          }
          break;
        }
      }
    }
    else
    {
      current_rep_->current_segment_.reset(); // start from beginning
    }
  }

  const CSegment* next_segment{nullptr};

  if (current_rep_->current_segment_)
    next_segment = &*current_rep_->current_segment_;
  else
    next_segment = current_rep_->Timeline().GetFront();

  if (!next_segment && current_adp_->GetStreamType() != StreamType::SUBTITLE)
  {
    //! @todo: THIS MUST BE CHANGED - !! BUG !!
    //! this will broken/stop playback on live streams when adaptive stream change stream quality (so representation)
    //! and there are no new segments because not immediately available (despite child manifest updated),
    //! because we cant be ensure next segment without wait the appropriate timing...imo its not the right place here.
    //! Can be reproduced with HLS live and by using "Stream selection" setting to "Test" by switching per 1 segment
    absolute_position_ = ~0;
    LOG::LogF(LOGERROR, "[AS-%u] No next segment, force stop stream (this is a known issue)",
              clsId);
    return true;
  }

  absolute_position_ = 0;

  if (current_rep_->HasInitSegment() &&
      (m_segBuffers.IsEmpty() || !m_segBuffers.Front().segment.IsInitialization()))
  {
    SegmentBuffer segBuffer;
    segBuffer.rep = current_rep_;
    segBuffer.segment = *current_rep_->GetInitSegment();
    m_segBuffers.Push(std::move(segBuffer));

    // LOG::LogF(LOGDEBUG, "[AS-%u] BUFFERS QUEUE - Add init segment (representation id %s)", clsId,
    //           current_rep_->GetId().c_str());
  }

  // Reset the event for the next one
  m_startEvent = EVENT_TYPE::NONE;

  if (!current_rep_->Timeline().Get(0))
  {
    LOG::LogF(LOGERROR, "[AS-%u] Segment at position 0 not found from representation id: %s", clsId,
              current_rep_->GetId().c_str());
    return false;
  }

  if (next_segment)
  {
    currentPTSOffset_ =
        (next_segment->startPTS_ * current_rep_->timescale_ext_) / current_rep_->timescale_int_;
    absolutePTSOffset_ =
        (current_rep_->Timeline().Get(0)->startPTS_ * current_rep_->timescale_ext_) /
        current_rep_->timescale_int_;
  }

  current_rep_->SetIsEnabled(true);
  return true;
}

bool adaptive::AdaptiveStream::ensureSegment()
{
  // NOTE: Some demuxers may call ensureSegment more times to try make more attempts when it return false.

  // This method can be called more times so prevent to switch to other segments when stream quality change has been requested
  if (m_startEvent == EVENT_TYPE::REP_CHANGE)
    return true;

  // Process a new segment when the previous one has been fully read by checking buffer size.
  // Note:
  //  When a stream does not have an initialization segment, m_segBuffers is empty from the beginning.
  //  When a stream has an initialization segment, m_segBuffers contain the init segment (added by AdaptiveStream::start_stream).
  if (m_segBuffers.IsEmpty() || (m_segBuffers.Front().BufferSize() != 0 &&
                                 segment_read_pos_ >= m_segBuffers.Front().BufferSize()))
  {
    if (!m_segBuffers.IsEmpty() && m_segBuffers.Front().State() == BufferState::DOWNLOADING)
    {
      // Although the reading position has reached the end, the segment status may not yet be updated
      // so wait for the mutex release, otherwise segment buffers PopFront will cause incorrect buffer updates
      std::lock_guard<std::mutex> lckWorker(thread_data_->mutexWorker);
    }

    if (m_fixateInitialization && !m_segBuffers.IsEmpty() &&
        m_segBuffers.Front().segment.IsInitialization())
    {
      // Force stop operations at initialization segment
      // This occurs when the demuxer (e.g. webm) is initialized
      m_fixateInitialization = false;
      return false;
    }

    // lock live segment updates
    std::lock_guard<adaptive::AdaptiveTree::TreeUpdateThread> lckUpdTree(m_tree->GetTreeUpdMutex());

    if (m_tree->HasManifestUpdatesSegs())
    {
      // Limit requests with an interval of at least 1 second,
      // to avoid overloading servers with too requests
      if (SecondsSinceUpdate() > 1)
      {
        m_tree->OnRequestSegments(current_period_, current_adp_, current_rep_);
        lastUpdated_ = std::chrono::system_clock::now();
      }
    }

    // Remove the consumed segment, to proceed to the next one
    m_segBuffers.PopFront();

    // Check if the stream (representation) quality has been changed
    if (!m_segBuffers.IsEmpty() && m_segBuffers.Front().rep != current_rep_)
    {
      const SegmentBuffer& currSegBuff = m_segBuffers.FrontSeg();
      current_rep_->SetIsEnabled(false);
      current_rep_ = currSegBuff.rep;
      current_rep_->current_segment_ = currSegBuff.segment;
      current_rep_->SetIsEnabled(true);

      m_startEvent = EVENT_TYPE::REP_CHANGE;

      // When OnStreamChange is called, the Session::CheckChange will signal kodi to reopen the stream with the changed quality.
      // If a stream requires an init segment AdaptiveStream::start_stream expects to have the init segment
      // already in the buffer queue, so at this point m_segBuffers.Front() should already return the init segment
      if (observer_)
        observer_->OnStreamChange(this);
    }

    // Get the next segment in download/downloaded
    const CSegment* nextSegment{nullptr};

    if (!m_segBuffers.IsEmpty())
    {
      const SegmentBuffer& currSegBuff = m_segBuffers.Front();
      if (currSegBuff.State() == BufferState::INVALID || currSegBuff.State() == BufferState::NONE)
      {
        LOG::LogF(LOGWARNING,
                  "[AS-%u] Not valid buffer segment (status %i, rep. id \"%s\", period id \"%s\")",
                  clsId, currSegBuff.State(), current_rep_->GetId().c_str(),
                  current_period_->GetId().c_str());
        segment_read_pos_ = 0;
        return false;
      }
      // Note: In live streaming, the segments stored in the buffers may have expired
      // because they depends on timeshiftbuffer, so you will not be able to find
      // the same segment in the timeline (because removed by a manifest update)
      nextSegment = &currSegBuff.segment;
    }
    else
    {
      // if no segment: EOS or needs manifest update
      nextSegment = current_rep_->GetNextSegment();
    }

    if (nextSegment)
    {
      if (!nextSegment->IsInitialization())
      {
        currentPTSOffset_ =
            (nextSegment->startPTS_ * current_rep_->timescale_ext_) / current_rep_->timescale_int_;

        uint64_t absPtsOffset;

        if (current_rep_->Timeline().IsEmpty())
          absPtsOffset = nextSegment->startPTS_;
        else
          absPtsOffset = current_rep_->Timeline().GetFront()->startPTS_;

        absolutePTSOffset_ =
            (absPtsOffset * current_rep_->timescale_ext_) / current_rep_->timescale_int_;

        current_rep_->current_segment_ = *nextSegment;

        if (observer_ && nextSegment->startPTS_ != NO_PTS_VALUE)
        {
          observer_->OnSegmentChanged(this);
        }
      }

      ResetSegment(*nextSegment);

      if (m_startEvent == EVENT_TYPE::REP_CHANGE)
        return false;

      // Determine the segment to add to the queue of buffers
      const CSegment* queueSegment{nullptr};
      CRepresentation* newRep{nullptr};
      if (m_segBuffers.IsEmpty())
      {
        newRep = current_rep_;
        queueSegment = nextSegment;
      }
      else
      {
        // Continue from last segment added into buffers
        auto& lastSegBuff = m_segBuffers.BackSeg();
        newRep = lastSegBuff.rep;
        queueSegment = lastSegBuff.rep->Timeline().GetNext(lastSegBuff.segment);
      }

      // Add to the buffers queue the next segment(s)
      //! @TODO: investigate to live delay it should not put in download not fully available segments
      while (queueSegment && !m_segBuffers.IsBufferFull())
      {
        if (!m_segBuffers.IsEmpty())
        {
          // The representation from the last added segment buffer
          CRepresentation* prevRep = newRep;

          newRep = m_tree->GetRepChooser()->GetNextRepresentation(current_adp_, prevRep);

          //! @todo: There is the possibility that stream quality switching happen frequently in very short time,
          //! so if OnStreamChange is used on a parser, it could overload servers of manifest requests
          //! a minimum interval should be considered to avoid too switches in a too short period of time
          if (newRep != prevRep) // Stream quality changed
          {
            // If the representation has been changed, segments may have to be generated (DASH)
            if (newRep->Timeline().IsEmpty())
              GenerateSidxSegments(newRep);

            m_tree->OnStreamChange(current_period_, current_adp_, prevRep, newRep);
            // Try aligning the segment to ensure that it exists on the changed representation
            m_tree->OnAlignSegment(current_period_, current_adp_, prevRep, newRep, queueSegment);
            // Do not allow quality change with only init segment (if used), ensure at least one segment
            if (!queueSegment)
            {
              LOG::LogF(LOGWARNING,
                        "[AS-%u] Cannot switch stream quality, no segment available in the next "
                        "representation id: %s",
                        clsId, newRep->GetId().c_str());
              newRep = prevRep;
              break;
            }

            if (newRep->HasInitSegment())
            {
              // Add to the buffer the initialization segment
              // it will be loaded when kodi reopen the stream to change quality
              SegmentBuffer segInitBuffer;
              segInitBuffer.rep = newRep;
              segInitBuffer.segment = *newRep->GetInitSegment();
              m_segBuffers.Push(std::move(segInitBuffer));

              // LOG::LogF(LOGDEBUG,
              //           "[AS-%u] BUFFERS QUEUE - Add init segment (representation id %s)",
              //           clsId, newRep->GetId().c_str());
            }
          }
        }

        SegmentBuffer segBuffer;
        segBuffer.rep = newRep;
        segBuffer.segment = *queueSegment;

        // LOG::LogF(LOGDEBUG, "[AS-%u] BUFFERS QUEUE - Add segment (number %llu, PTS %llu)",
        //           clsId, queueSegment->m_number, queueSegment->startPTS_);
        m_segBuffers.Push(std::move(segBuffer));

        // Some sample reader dont use initialization segment so when they are in initialization phase
        // in order to get info need to parse a media segment, but we want avoid downloading more segments,
        // this is a workaround, for more info see todo "InputStream resume playback design problem" in the main.cpp
        if (!m_isAllowedBufferQueue)
          break;

        // Check if there is a following segment (add it to the queue at next iteration)
        if (!m_segBuffers.IsBufferFull())
          queueSegment = newRep->Timeline().GetNext(*queueSegment);
      }
    }
    else if (!m_tree->IsLastSegment(current_period_, current_rep_, current_rep_->current_segment_))
    {
      if (m_segBuffers.IsEmpty() && !current_rep_->IsWaitForSegment())
      {
        current_rep_->SetIsWaitForSegment(true);
        LOG::LogF(LOGDEBUG, "[AS-%u] Begin WaitForSegment stream rep. id \"%s\" period id \"%s\"",
                  clsId, current_rep_->GetId().c_str(), current_period_->GetId().c_str());
      }
      return false;
    }
    else if (current_rep_->IsWaitForSegment() &&
             (m_tree->HasManifestUpdates() || m_tree->HasManifestUpdatesSegs()))
    {
      return false;
    }
    else if (m_segBuffers.IsEmpty())
    {
      LOG::LogF(LOGDEBUG, "[AS-%u] End of segments", clsId);
      return false;
    }
  }
  return true;
}

uint32_t adaptive::AdaptiveStream::read(void* buffer, uint32_t bytesToRead)
{
  if (ensureSegment() && bytesToRead > 0)
  {
    if (m_segBuffers.IsEmpty())
      return 0;

    SegmentBuffer& currSegBuffer = m_segBuffers.Front();

    size_t avail = currSegBuffer.BufferSize() - segment_read_pos_;

    {
      std::unique_lock<std::mutex> lckrw(thread_data_->mutexRW);
      // Wait until we have all data from the chunked download
      while (avail < bytesToRead &&
             (currSegBuffer.State() == BufferState::QUEUED ||
              currSegBuffer.State() == BufferState::DOWNLOADING) &&
             thread_data_->State() == THREADDATA::ThState::RUNNING)
      {
        thread_data_->cvRW.wait(lckrw);
        avail = currSegBuffer.BufferSize() - segment_read_pos_;
      }
    }

    if (avail > bytesToRead)
      avail = bytesToRead;

    segment_read_pos_ += avail;
    absolute_position_ += avail;

    if (avail == bytesToRead)
    {
      currSegBuffer.CopyBufferTo(buffer, segment_read_pos_ - avail, avail);
      return static_cast<uint32_t>(avail);
    }
  }

  return 0;
}

bool adaptive::AdaptiveStream::ReadFullBuffer(std::vector<uint8_t>& buffer)
{
  if (ensureSegment())
  {
    if (m_segBuffers.IsEmpty())
      return false;

    SegmentBuffer& currSegBuffer = m_segBuffers.Front();

    {
      std::unique_lock<std::mutex> lckrw(thread_data_->mutexRW);
      // Wait until we have all data from the chunked download
      while ((currSegBuffer.State() == BufferState::QUEUED ||
              currSegBuffer.State() == BufferState::DOWNLOADING) &&
             thread_data_->State() == THREADDATA::ThState::RUNNING)
      {
        thread_data_->cvRW.wait(lckrw);
      }
    }

    buffer = currSegBuffer.ReadBuffer();
    // Signal we have read until the last byte
    segment_read_pos_ = buffer.size();

    // The state is updated after read/write operations
    if (currSegBuffer.State() == BufferState::DOWNLOADING)
    {
      // So wait for the mutex release, to ensure that the segment state is updated
      std::lock_guard<std::mutex> lckWorker(thread_data_->mutexWorker);
    }

    if (currSegBuffer.State() == BufferState::INVALID)
      buffer.clear();

    return true;
  }

  return false;
}

bool adaptive::AdaptiveStream::GetBufferSize(uint64_t& size)
{
  // Unused method (see CAdaptiveByteStream::GetSize) intended for FragmentedSampleReader clarification:
  // On this sample reader we force the MP4 demuxer in a somewhat hacky sequential read, so,
  // we assume that the stream is like a single big file from start to end, rather than splitted to segments.
  // To do this we do not provide the buffer segment size to force unknown size state in the MP4 demuxer.
  // This behaviour cause some particular behaviours on MP4 demuxer callback methods such as:
  //  AdaptiveStream::seek/read can be called to request data exceeding the actual size of the buffer
  //  AdaptiveStream::tell can get the absolute position from different segments
  // These methods will be called continuously without any way of knowing when the segment data actually ends
  // and so AdaptiveStream::ensureSegment take care to continue the stream in a sequential way.
  // Despite this, when the AdaptiveStream::read or AdaptiveStream::seek methods return false,
  // they will generally cause EOS, so if there is no stream quality change signalled
  // or no further periods, playback will end.
  return false;
}

uint64_t adaptive::AdaptiveStream::tell()
{
  // if the current segment has already been read completely,
  // call "Read" to move on to the next one and so update the absolute position
  if (!m_fixateInitialization)
    read(0, 0);

  return absolute_position_;
}

bool adaptive::AdaptiveStream::seek(uint64_t const pos, bool& isEos)
{
  if (m_segBuffers.IsEmpty())
  {
    // NOTE: FragmentedSampleReader (FMP4) hacky sequential read:
    // When FragmentedSampleReader run ReadNextSample, its possible that AP4_LinearReader
    // try to go to the start of the next fragment even though it has already reached the end of the file
    // (of the last segment fed) so AP4_LinearReader::AdvanceFragment do a callback to this method.
    // This method assume to read always from the current segment,
    // but if there are no segments, on live streams it could mean that we are waiting for the next
    // manifest update or the insertion of new live segments.
    // At this point, since the value of "IsWaitForSegment" could have been changed to False
    // just before this "seek" callback, we must call "ensureSegment" to guarantee the next segment
    // and obtain an updated value for "IsWaitForSegment" to determine a EOS
    if (ensureSegment())
      return true;

    if (!current_rep_->IsWaitForSegment())
      isEos = true;

    return false;
  }

  SegmentBuffer& currSegBuffer = m_segBuffers.Front();

  if (pos > (absolute_position_ - segment_read_pos_) + currSegBuffer.BufferSize())
  {
    {
      std::unique_lock<std::mutex> lckrw(thread_data_->mutexRW);
      // Wait until we have all data from the chunked download
      while (pos > (absolute_position_ - segment_read_pos_) + currSegBuffer.BufferSize() &&
             (currSegBuffer.State() == BufferState::QUEUED ||
              currSegBuffer.State() == BufferState::DOWNLOADING) &&
             thread_data_->State() == THREADDATA::ThState::RUNNING)
      {
        thread_data_->cvRW.wait(lckrw);
      }
    }
  }

  segment_read_pos_ = static_cast<size_t>(pos - (absolute_position_ - segment_read_pos_));

  if (segment_read_pos_ > currSegBuffer.BufferSize())
  {
    segment_read_pos_ = currSegBuffer.BufferSize();
    return false;
  }

  absolute_position_ = pos;
  return true;
}

uint64_t adaptive::AdaptiveStream::getMaxTimeMs()
{
  const CSegment* lastSeg = current_rep_->Timeline().GetBack();
  if (!lastSeg)
    return 0;

  const uint64_t maxPts =
      (lastSeg->m_endPts * current_rep_->timescale_ext_) / current_rep_->timescale_int_;

  if (absolutePTSOffset_ > maxPts)
  {
    LOG::LogF(LOGERROR, "Absolute PTS offset (%llu) exceeds max PTS (%llu)", absolutePTSOffset_,
              maxPts);
    return 0;
  }

  return (maxPts - absolutePTSOffset_) / 1000;
}

void adaptive::AdaptiveStream::Disable()
{
  // Preserve following events
  if (m_startEvent == EVENT_TYPE::REP_CHANGE)
    return;

  // Prepare it for the future event
  m_startEvent = EVENT_TYPE::STREAM_ENABLE;
}

void adaptive::AdaptiveStream::UpdateCurrentSegment(const PLAYLIST::CSegment& newSegment)
{
  if (AlignBufferToSegment(newSegment))
  {
    current_rep_->current_segment_ = m_segBuffers.Front().segment;
  }
  else
  {
    // EnsureSegment always loads the segment following the one specified as current, then sets the previous one
    const CSegment* prevSeg = current_rep_->Timeline().GetPrevious(newSegment);
    if (prevSeg)
      current_rep_->current_segment_ = *prevSeg;
    else
      current_rep_->current_segment_.reset();
  }

  absolute_position_ = 0;
  segment_read_pos_ = 0;
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

bool adaptive::AdaptiveStream::seek_time(double seek_seconds)
{
  if (!current_rep_)
    return false;

  if (current_rep_->IsSubtitleFileStream())
    return true;

  std::lock_guard<adaptive::AdaptiveTree::TreeUpdateThread> lckUpdTree(m_tree->GetTreeUpdMutex());

  const uint64_t pts = static_cast<uint64_t>(seek_seconds * current_rep_->GetTimescale());
  const CSegment* seekSeg = current_rep_->Timeline().FindByPTSOrNext(pts);

  if (!seekSeg)
    return false;

  UpdateCurrentSegment(*seekSeg);

  return true;
}

bool adaptive::AdaptiveStream::waitingForSegment() const
{
  if (m_tree->IsLive() && thread_data_->State() == THREADDATA::ThState::RUNNING)
  {
    std::lock_guard<adaptive::AdaptiveTree::TreeUpdateThread> lckUpdTree(m_tree->GetTreeUpdMutex());

    // Some manifests require segments to be generated and managed by the client
    // so they are not provided by the server through periodic manifest updates
    m_tree->InsertLiveSegment(current_period_, current_adp_, current_rep_);

    // Although IsWaitForSegment may be true, do not anticipate the wait for segments
    // if there are still segments in the buffer that can be read and/or downloaded
    return current_rep_ && current_rep_->IsWaitForSegment() && m_segBuffers.IsEmpty();
  }
  return false;
}

void adaptive::AdaptiveStream::FixateInitialization(bool on)
{
  m_fixateInitialization = on;
}

bool adaptive::AdaptiveStream::GenerateSidxSegments(PLAYLIST::CRepresentation* rep)
{
  const ContainerType containerType = rep->GetContainerType();
  if (containerType == ContainerType::NOTYPE)
    return false;
  else if (containerType != ContainerType::MP4 && containerType != ContainerType::WEBM)
  {
    LOG::LogF(LOGERROR,
              "[AS-%u] Cannot generate segments from SIDX on repr id \"%s\" with container \"%i\"",
              clsId, rep->GetId().c_str(), static_cast<int>(containerType));
    return false;
  }

  // Get the byte ranges to download the index segment to generate media segments from SIDX atom
  CSegment seg;
  // SetIsInitialization is set just to ignore fileOffset on PrepareDownload
  // the init segment will be set to representation by ParseIndexRange
  seg.SetIsInitialization(true);

  if (rep->HasSegmentBase())
  {
    auto& segBase = rep->GetSegmentBase();
    if (segBase->GetIndexRangeEnd() > 0)
    {
      // No init segment, we need to create it, so get all bytes from start to try get MOOV atom
      seg.range_begin_ = rep->HasInitSegment() ? segBase->GetIndexRangeBegin() : 0;
      seg.range_end_ = segBase->GetIndexRangeEnd();
    }
    else if (rep->HasInitSegment())
    {
      seg = *rep->GetInitSegment();
    }
    else
      return false;
  }
  else
  {
    LOG::LogF(LOGERROR,
              "[AS-%u] Cannot generate segments from SIDX on repr id \"%s\", "
              "due to missing data range positions",
              clsId, rep->GetId().data());
    return false;
  }

  std::vector<uint8_t> sidxBuffer;
  DownloadInfo downloadInfo;

  if (PrepareDownload(rep, seg, downloadInfo) && Download(downloadInfo, sidxBuffer) &&
      parseIndexRange(rep, sidxBuffer))
  {
    return true;
  }

  return false;
}

void adaptive::AdaptiveStream::Stop()
{
  if (thread_data_)
  {
    // Stop downloads
    thread_data_->StopDownloads();
    // Wait that worker exit
    std::lock_guard<std::mutex> lckWorker(thread_data_->mutexWorker);
  }

  // Disable representation only after stopped the worker
  // otherwise if read some segments may invalidate this change
  if (current_rep_)
    current_rep_->SetIsEnabled(false);
}

void adaptive::AdaptiveStream::clear()
{
  current_adp_ = 0;

  if (current_rep_)
    current_rep_->current_segment_.reset();

  current_rep_ = 0;
}

void adaptive::AdaptiveStream::Dispose()
{
  m_segBuffers.Reset();

  segment_read_pos_ = 0;
  absolute_position_ = 0;

  if (thread_data_)
  {
    delete thread_data_;
    thread_data_ = nullptr;
  }
}

void adaptive::AdaptiveStream::THREADDATA::Initialize(AdaptiveStream* parent)
{
  // Already set the state to RUNNING will allow the thread
  // to start immediately without waiting for condition notifications
  m_state = ThState::RUNNING;

  m_downloadThread = std::thread(&AdaptiveStream::worker, parent);
}

void adaptive::AdaptiveStream::THREADDATA::StopDownloads()
{
  m_state = ThState::STOPPED;
}

void adaptive::AdaptiveStream::THREADDATA::PauseDownloads()
{
  m_state = ThState::PAUSED;
}

void adaptive::AdaptiveStream::THREADDATA::StartDownloads()
{
  m_state = ThState::RUNNING;
  cvState.notify_all();
}
