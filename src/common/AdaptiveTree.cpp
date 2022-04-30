/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AdaptiveTree.h"
#include "Chooser.h"

#include "../utils/UrlUtils.h"
#include "../utils/log.h"

#ifndef INPUTSTREAM_TEST_BUILD
#include <kodi/Filesystem.h>
#include <kodi/General.h>
#endif

#include <algorithm>
#include <chrono>
#include <stdlib.h>
#include <string.h>

using namespace UTILS;

namespace adaptive
{
  void AdaptiveTree::Segment::SetRange(const char *range)
  {
    const char *delim(strchr(range, '-'));
    if (delim)
    {
      range_begin_ = strtoull(range, 0, 10);
      range_end_ = strtoull(delim + 1, 0, 10);
    }
    else
      range_begin_ = range_end_ = 0;
  }

  void AdaptiveTree::Segment::Copy(const Segment* src)
  {
    *this = *src;
  }

  AdaptiveTree::AdaptiveTree(const UTILS::PROPERTIES::KodiProperties& kodiProps,
                             CHOOSER::IRepresentationChooser* reprChooser)
    : m_kodiProps(kodiProps),
      m_reprChooser(reprChooser),
      m_streamHeaders(kodiProps.m_streamHeaders),
      current_period_(nullptr),
      next_period_(nullptr),
      parser_(0),
      currentNode_(0),
      segcount_(0),
      overallSeconds_(0),
      stream_start_(0),
      available_time_(0),
      base_time_(0),
      live_delay_(0),
      minPresentationOffset(0),
      has_timeshift_buffer_(false),
      has_overall_seconds_(false),
      updateInterval_(~0),
      updateThread_(nullptr),
      lastUpdated_(std::chrono::system_clock::now())
  {
    // Convenience way to share common addon settings we avoid
    // calling the API many times to improve parsing performance
    m_settings.m_bufferAssuredDuration =
        static_cast<uint32_t>(kodi::addon::GetSettingInt("ASSUREDBUFFERDURATION"));
    m_settings.m_bufferMaxDuration =
        static_cast<uint32_t>(kodi::addon::GetSettingInt("MAXBUFFERDURATION"));
  }

  AdaptiveTree::~AdaptiveTree()
  {
    has_timeshift_buffer_ = false;
    if (updateThread_)
    {
      {
        std::lock_guard<std::mutex> lck(updateMutex_);
        updateVar_.notify_one();
      }
      updateThread_->join();
      delete updateThread_;
    }

    std::lock_guard<std::mutex> lck(treeMutex_);
    for (std::vector<Period*>::const_iterator bp(periods_.begin()), ep(periods_.end()); bp != ep; ++bp)
      delete *bp;
  }

  //! @todo: CheckHDCP we will need to move this method in Session class to
  //! avoid store the CAPS here, then call the Session method from AdaptiveTree
  //! To be done after Session refactor
  void AdaptiveTree::CheckHDCP()
  {
    //! @todo: is needed to implement an appropriate CP check to
    //! remove HDCPOVERRIDE setting workaround

    if (m_decrypterCaps.empty())
      return;

    uint32_t adpIndex{0};
    adaptive::AdaptiveTree::AdaptationSet* adp{nullptr};
    while ((adp = GetAdaptationSet(adpIndex++)))
    {
      if (adp->type_ != adaptive::AdaptiveTree::StreamType::VIDEO)
        continue;

      for (auto it = adp->representations_.begin(); it != adp->representations_.end();)
      {
        adaptive::AdaptiveTree::Representation* repr = *it;
        uint16_t hdcpVersion = m_decrypterCaps[repr->pssh_set_].hdcpVersion;
        int hdcpLimit = m_decrypterCaps[repr->pssh_set_].hdcpLimit;

        if (repr->hdcpVersion_ > hdcpVersion ||
            (hdcpLimit > 0 && repr->width_ * repr->height_ > hdcpLimit))
        {
          LOG::Log(LOGDEBUG, "Representation ID \"%s\" removed as not HDCP compliant",
                   repr->id.c_str());
          delete repr;
          it = adp->representations_.erase(it);
        }
        else
          it++;
      }
    }
  }

  void AdaptiveTree::FreeSegments(Period* period, Representation* rep)
  {
    for (auto& segment : rep->segments_.data) {
      --period->psshSets_[segment.pssh_set_].use_count_;
    }
    if ((rep->flags_ & (Representation::INITIALIZATION | Representation::URLSEGMENTS)) ==
        (Representation::INITIALIZATION | Representation::URLSEGMENTS))
    {
      rep->initialization_.url.clear();
    }
    rep->segments_.clear();
    rep->current_segment_ = nullptr;
  }

  bool AdaptiveTree::has_type(StreamType t)
  {
    if (periods_.empty())
      return false;

    for (std::vector<AdaptationSet*>::const_iterator b(periods_[0]->adaptationSets_.begin()), e(periods_[0]->adaptationSets_.end()); b != e; ++b)
      if ((*b)->type_ == t)
        return true;
    return false;
  }

  size_t AdaptiveTree::EstimateSegmentsCount(uint64_t duration, uint32_t timescale)
  {
    double lengthSecs{static_cast<double>(duration) / timescale};
    if (lengthSecs < 1)
      lengthSecs = 1;
    return static_cast<size_t>(overallSeconds_ / lengthSecs);
  }

  void AdaptiveTree::SetFragmentDuration(const AdaptationSet* adp, const Representation* rep, size_t pos, uint64_t timestamp, uint32_t fragmentDuration, uint32_t movie_timescale)
  {
    if (!has_timeshift_buffer_ || HasUpdateThread() ||
      (rep->flags_ & AdaptiveTree::Representation::URLSEGMENTS) != 0)
      return;

    //Get a modifiable adaptationset
    AdaptationSet *adpm(const_cast<AdaptationSet *>(adp));

    // Check if its the last frame we watch
    if (adp->segment_durations_.data.size())
    {
      if (pos == adp->segment_durations_.data.size() - 1)
      {
        adpm->segment_durations_.insert(static_cast<std::uint64_t>(fragmentDuration)*adp->timescale_ / movie_timescale);
      }
      else
      {
        ++const_cast<Representation*>(rep)->expired_segments_;
        return;
      }
    }
    else if (pos != rep->segments_.data.size() - 1)
      return;

    if (!rep->segments_.Get(pos))
    {
      LOG::LogF(LOGERROR, "Segment at position %zu not found from representation id: %s", pos,
                rep->id.c_str());
      return;
    }

    Segment segment(*(rep->segments_.Get(pos)));

    if (!timestamp)
    {
      LOG::LogF(LOGDEBUG, "Scale fragment duration: fdur:%u, rep-scale:%u, mov-scale:%u",
                fragmentDuration, rep->timescale_, movie_timescale);
      fragmentDuration = static_cast<std::uint32_t>((static_cast<std::uint64_t>(fragmentDuration)*rep->timescale_) / movie_timescale);
    }
    else
    {
      LOG::LogF(LOGDEBUG, "Fragment duration from timestamp: ts:%llu, base:%llu, s-pts:%llu",
                timestamp, base_time_, segment.startPTS_);
      fragmentDuration = static_cast<uint32_t>(timestamp - base_time_ - segment.startPTS_);
    }

    segment.startPTS_ += fragmentDuration;
    segment.range_begin_ += fragmentDuration;
    segment.range_end_ ++;

    LOG::LogF(LOGDEBUG, "Insert live segment: pts: %llu range_end: %llu", segment.startPTS_,
              segment.range_end_);

    for (std::vector<Representation*>::iterator b(adpm->representations_.begin()), e(adpm->representations_.end()); b != e; ++b)
      (*b)->segments_.insert(segment);
  }

  void AdaptiveTree::OnDataArrived(uint64_t segNum, uint16_t psshSet, uint8_t iv[16], const uint8_t *src, uint8_t *dst, size_t dstOffset, size_t dataSize)
  {
    memcpy(dst + dstOffset, src, dataSize);
  }

  uint16_t AdaptiveTree::insert_psshset(StreamType type,
                                        AdaptiveTree::Period* period,
                                        AdaptiveTree::AdaptationSet* adp)
  {
    if (!period)
      period = current_period_;
    if (!adp)
      adp = current_adaptationset_;

    if (!current_pssh_.empty())
    {
      Period::PSSH pssh;
      pssh.pssh_ = current_pssh_;
      pssh.defaultKID_ = current_defaultKID_;
      pssh.iv = current_iv_;
      pssh.adaptation_set_ = adp;
      switch (type)
      {
      case VIDEO: pssh.media_ = Period::PSSH::MEDIA_VIDEO; break;
      case AUDIO: pssh.media_ = Period::PSSH::MEDIA_AUDIO; break;
      case STREAM_TYPE_COUNT: pssh.media_ = Period::PSSH::MEDIA_VIDEO | Period::PSSH::MEDIA_AUDIO; break;
      default: pssh.media_ = 0; break;
      }
      return period->InsertPSSHSet(&pssh);
    }
    else
      return period->InsertPSSHSet(nullptr);
  }

  void AdaptiveTree::Representation::CopyBasicData(Representation* src)
  {
    url_ = src->url_;
    id = src->id;
    codecs_ = src->codecs_;
    codec_private_data_ = src->codec_private_data_;
    source_url_ = src->source_url_;
    bandwidth_ = src->bandwidth_;
    samplingRate_ = src->samplingRate_;
    width_ = src->width_;
    height_ = src->height_;
    fpsRate_ = src->fpsRate_;
    fpsScale_ = src->fpsScale_;
    aspect_ = src->aspect_;
    flags_ = src->flags_;
    hdcpVersion_ = src->hdcpVersion_;
    channelCount_ = src->channelCount_;
    nalLengthSize_ = src->nalLengthSize_;
    containerType_ = src->containerType_;
    timescale_ = src->timescale_;
    timescale_ext_ = src->timescale_ext_;
    timescale_int_ = src->timescale_int_;
  }

  void AdaptiveTree::AdaptationSet::CopyBasicData(AdaptiveTree::AdaptationSet* src)
  {
    representations_.resize(src->representations_.size());
    auto itRep = src->representations_.begin();
    for (Representation*& rep : representations_)
    {
      rep = new Representation();
      rep->CopyBasicData(*itRep++);
    }

    type_ = src->type_;
    timescale_ = src->timescale_;
    duration_ = src->duration_;
    startPTS_ = src->startPTS_;
    startNumber_ = src->startNumber_;
    impaired_ = src->impaired_;
    original_ = src->original_;
    default_ = src->default_;
    forced_ = src->forced_;
    language_ = src->language_;
    mimeType_ = src->mimeType_;
    base_url_ = src->base_url_;
    id_ = src->id_;
    group_ = src->group_;
    codecs_ = src->codecs_;
    audio_track_id_ = src->audio_track_id_;
    name_ = src->name_;
  }

  // Create a HLS master playlist copy (no representations)
  void AdaptiveTree::Period::CopyBasicData(AdaptiveTree::Period* period)
  {
    adaptationSets_.resize(period->adaptationSets_.size());
    auto itAdp = period->adaptationSets_.begin();
    for (AdaptationSet*& adp : adaptationSets_)
    {
      adp = new AdaptationSet();
      adp->CopyBasicData(*itAdp++);
    }

    base_url_ = period->base_url_;
    id_ = period->id_;
    timescale_ = period->timescale_;
    startNumber_ = period->startNumber_;

    start_ = period->start_;
    startPTS_ = period->startPTS_;
    duration_ = period->duration_;
    encryptionState_ = period->encryptionState_;
    included_types_ = period->included_types_;
    need_secure_decoder_ = period->need_secure_decoder_;
  }

  uint16_t AdaptiveTree::Period::InsertPSSHSet(PSSH* pssh)
  {
    if (pssh)
    {
      std::vector<Period::PSSH>::iterator pos(std::find(psshSets_.begin() + 1, psshSets_.end(), *pssh));
      if (pos == psshSets_.end())
        pos = psshSets_.insert(psshSets_.end(), *pssh);
      else if (!pos->use_count_)
        *pos = *pssh;

      ++psshSets_[pos - psshSets_.begin()].use_count_;
      return static_cast<uint16_t>(pos - psshSets_.begin());
    }
    else
    {
      ++psshSets_[0].use_count_;
      return 0;
    }
  }

  void AdaptiveTree::Period::RemovePSSHSet(uint16_t pssh_set)
  {
    for (std::vector<AdaptationSet*>::const_iterator ba(adaptationSets_.begin()), ea(adaptationSets_.end()); ba != ea; ++ba)
      for (std::vector<Representation*>::iterator br((*ba)->representations_.begin()), er((*ba)->representations_.end()); br != er;)
        if ((*br)->pssh_set_ == pssh_set)
        {
          delete *br;
          br = (*ba)->representations_.erase(br);
          er = (*ba)->representations_.end();
        }
        else
          ++br;
  }

  bool AdaptiveTree::PreparePaths(const std::string &url)
  {
    if (!URL::IsValidUrl(url))
    {
      LOG::LogF(LOGERROR, "URL not valid (%s)", url.c_str());
      return false;
    }
    manifest_url_ = url;
    base_url_ = URL::RemoveParameters(url);
    return true;
  }

  void AdaptiveTree::PrepareManifestUrl(const std::string &url, const std::string &manifestUpdateParam)
  {
    manifest_url_ = url;

    if (manifestUpdateParam.empty())
    {
      update_parameter_ = URL::GetParametersFromPlaceholder(manifest_url_, "$START_NUMBER$");
      manifest_url_.resize(manifest_url_.size() - update_parameter_.size());
    }
    else
      update_parameter_ = manifestUpdateParam;
  }

  std::string AdaptiveTree::BuildDownloadUrl(const std::string& url) const
  {
    if (URL::IsUrlAbsolute(url))
      return url;

    return URL::Join(base_url_, url);
  }

  void AdaptiveTree::SortTree()
  {
    for (std::vector<Period*>::const_iterator bp(periods_.begin()), ep(periods_.end()); bp != ep; ++bp)
    {
      // Merge VIDEO & AUDIO adaption sets
      for (std::vector<AdaptationSet*>::iterator ba((*bp)->adaptationSets_.begin()), ea((*bp)->adaptationSets_.end()); ba != ea;)
      {
        if (((*ba)->type_ == AUDIO || (*ba)->type_ == VIDEO) && ba + 1 != ea && AdaptationSet::mergeable(*ba, *(ba + 1)))
        {
          for (size_t i(1); i < (*bp)->psshSets_.size(); ++i)
            if ((*bp)->psshSets_[i].adaptation_set_ == *ba)
              (*bp)->psshSets_[i].adaptation_set_ = *(ba + 1);

          (*(ba + 1))->representations_.insert((*(ba + 1))->representations_.end(), (*ba)->representations_.begin(), (*ba)->representations_.end());
          (*ba)->representations_.clear();
          ba = (*bp)->adaptationSets_.erase(ba);
          ea = (*bp)->adaptationSets_.end();
        }
        else
          ++ba;
      }

      std::stable_sort((*bp)->adaptationSets_.begin(), (*bp)->adaptationSets_.end(), AdaptationSet::compare);

      for (std::vector<AdaptationSet*>::const_iterator ba((*bp)->adaptationSets_.begin()), ea((*bp)->adaptationSets_.end()); ba != ea; ++ba)
      {
        std::sort((*ba)->representations_.begin(), (*ba)->representations_.end(), Representation::compare);
        for (std::vector<Representation*>::iterator br((*ba)->representations_.begin()), er((*ba)->representations_.end()); br != er; ++br)
          (*br)->SetScaling();
      }
    }
  }

  void AdaptiveTree::RefreshUpdateThread()
  {
    if (HasUpdateThread())
    {
      std::lock_guard<std::mutex> lck(updateMutex_);
      updateVar_.notify_one();
    }
  }

  void AdaptiveTree::StartUpdateThread()
  {
    if (!updateThread_ && ~updateInterval_ && has_timeshift_buffer_ && !update_parameter_.empty())
      updateThread_ = new std::thread(&AdaptiveTree::SegmentUpdateWorker, this);
  }

  void AdaptiveTree::SegmentUpdateWorker()
  {
    std::unique_lock<std::mutex> updLck(updateMutex_);
    while (~updateInterval_ && has_timeshift_buffer_)
    {
      if (updateVar_.wait_for(updLck, std::chrono::milliseconds(updateInterval_)) == std::cv_status::timeout)
      {
        std::lock_guard<std::mutex> lck(treeMutex_);
        lastUpdated_ = std::chrono::system_clock::now();
        RefreshLiveSegments();
      }
    }
  }

  bool AdaptiveTree::download(const std::string& url,
                              const std::map<std::string, std::string>& reqHeaders,
                              std::stringstream& data,
                              HTTPRespHeaders& respHeaders)
  {
    // open the file
    kodi::vfs::CFile file;
    if (!file.CURLCreate(url))
      return false;

    file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "seekable", "0");
    file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "acceptencoding", "gzip");

    for (const auto& entry : reqHeaders)
    {
      file.CURLAddOption(ADDON_CURL_OPTION_HEADER, entry.first.c_str(), entry.second.c_str());
    }

    if (!file.CURLOpen(ADDON_READ_CHUNKED | ADDON_READ_NO_CACHE))
    {
      LOG::Log(LOGERROR, "CURLOpen returned an error, download failed: %s", url.c_str());
      return false;
    }

    respHeaders.m_effectiveUrl = file.GetPropertyValue(ADDON_FILE_PROPERTY_EFFECTIVE_URL, "");

    // read the file
    static const size_t bufferSize{16 * 1024}; // 16 Kbyte
    std::vector<char> bufferData(bufferSize);
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
      else if (byteRead == 0) // EOF or undetectable error
      {
        isEOF = true;
      }
      else
      {
        data.write(bufferData.data(), byteRead);
      }
    }

    if (isEOF)
    {
      long dataSizeBytes{static_cast<long>(data.tellp())};
      if(dataSizeBytes > 0)
      {
        // Get body lenght (could be gzip compressed)
        std::string contentLengthStr{
            file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, "Content-Length")};
        long contentLength{std::atol(contentLengthStr.c_str())};
        if (contentLength == 0)
          contentLength = dataSizeBytes;
        
        double downloadSpeed{file.GetFileDownloadSpeed()};
        // The download speed with small file sizes are not accurate
        // we should have at least 512Kb to have a sufficient acceptable value
        // to calculate the bandwidth, then we make a proportion for cases
        // with less than 512Kb to have a better value
        static const int minSize{512 * 1024};
        if (contentLength < minSize)
          downloadSpeed = (downloadSpeed / contentLength) * minSize;

        respHeaders.m_etag = file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, "etag");
        respHeaders.m_lastModified =
            file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, "last-modified");

        // We set the download speed to calculate the initial network bandwidth
        m_reprChooser->SetDownloadSpeed(downloadSpeed);

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

    file.Close();
    return false;
  }

} // namespace
