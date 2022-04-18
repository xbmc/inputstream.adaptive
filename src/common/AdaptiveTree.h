/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "expat.h"

#include "../utils/PropertiesUtils.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <inttypes.h>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef INPUTSTREAM_TEST_BUILD
#include "../test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

 // Forward namespace/class
namespace CHOOSER
{
class IRepresentationChooser;
}

namespace adaptive
{
  

template<typename T>
struct ATTR_DLL_LOCAL SPINCACHE
{
  /*! \brief Get the <T> value pointer from the specified position
   *  \param pos The position of <T>
   *  \return <T> value pointer, otherwise nullptr if not found
   */
  const T* Get(size_t pos) const
  {
    if (!~pos)
      return nullptr;
    size_t realPos = basePos + pos;
    if (realPos >= data.size())
    {
      realPos -= data.size();
      if (realPos == basePos)
        return nullptr;
    }
    return &data[realPos];
  }

  /*! \brief Get the <T> value pointer from the specified position
   *  \param pos The position of <T>
   *  \return <T> value pointer, otherwise nullptr if not found
   */
  T* Get(size_t pos)
  {
    if (!~pos)
      return nullptr;
    size_t realPos = basePos + pos;
    if (realPos >= data.size())
    {
      realPos -= data.size();
      if (realPos == basePos)
        return nullptr;
    }
    return &data[realPos];
  }

  /*! \brief Get index position of <T> value pointer
   *  \param elem The <T> pointer to get the position
   *  \return The index position
   */
  const size_t GetPosition(const T* elem) const
  {
    size_t realPos = elem - &data[0];
    if (realPos < basePos)
      realPos += data.size() - basePos;
    else
      realPos -= basePos;
    return realPos;
  };

  void insert(const T &elem)
  {
    data[basePos] = elem;
    ++basePos;
    if (basePos == data.size())
      basePos = 0;
  }

  void swap(SPINCACHE<T> &other)
  {
    data.swap(other.data);
    std::swap(basePos, other.basePos);
  }

  void clear()
  {
    data.clear();
    basePos = 0;
  }

  bool empty() const { return data.empty(); };

  size_t size() const { return data.size(); };

  size_t basePos{0};
  std::vector<T> data;
};

struct ATTR_DLL_LOCAL HTTPRespHeaders {

  std::string m_effectiveUrl;
  std::string m_etag; // etag header
  std::string m_lastModified; // last-modified header
};

class ATTR_DLL_LOCAL AdaptiveTree
{
public:
  struct Settings
  {
    uint32_t m_bufferAssuredDuration{60};
    uint32_t m_bufferMaxDuration{120};
  };

  enum StreamType
  {
    NOTYPE,
    VIDEO,
    AUDIO,
    SUBTITLE,
    STREAM_TYPE_COUNT
  };

  enum ContainerType : uint8_t
  {
    CONTAINERTYPE_NOTYPE,
    CONTAINERTYPE_INVALID,
    CONTAINERTYPE_MP4,
    CONTAINERTYPE_TS,
    CONTAINERTYPE_ADTS,
    CONTAINERTYPE_WEBM,
    CONTAINERTYPE_MATROSKA,
    CONTAINERTYPE_TEXT,
  };

  enum
  {
    ENCRYTIONSTATE_UNENCRYPTED = 0,
    ENCRYTIONSTATE_ENCRYPTED = 1,
    ENCRYTIONSTATE_SUPPORTED = 2
  };

  enum PREPARE_RESULT
  {
    PREPARE_RESULT_FAILURE,
    PREPARE_RESULT_OK,
    PREPARE_RESULT_DRMCHANGED,
    PREPARE_RESULT_DRMUNCHANGED,
  };

  // Node definition
  struct Segment
  {
    void SetRange(const char *range);
    void Copy(const Segment* src);
    uint64_t range_begin_ = 0; //Either byterange start or timestamp or ~0
    uint64_t range_end_ = 0; //Either byterange end or sequence_id if range_begin is ~0
    std::string url;
    uint64_t startPTS_ = 0;
    uint64_t m_duration = 0; // If available gives the media duration of a segment (depends on type of stream e.g. HLS)
    uint16_t pssh_set_ = 0;
  };

  struct SegmentTemplate
  {
    SegmentTemplate() : timescale(0), duration(0) {};
    std::string initialization;
    std::string media;
    unsigned int timescale, duration;
  };

  struct Representation
  {
    Representation() :bandwidth_(0), samplingRate_(0), width_(0), height_(0), fpsRate_(0), fpsScale_(1), aspect_(0.0f),
      flags_(0), hdcpVersion_(0), indexRangeMin_(0), indexRangeMax_(0), channelCount_(0), nalLengthSize_(0), pssh_set_(0), expired_segments_(0),
      containerType_(AdaptiveTree::CONTAINERTYPE_MP4), startNumber_(1), ptsOffset_(0), nextPts_(0), duration_(0), timescale_(0), current_segment_(nullptr)
    {
      initialization_.range_begin_ = initialization_.range_end_ = ~0ULL;
    };
    void CopyBasicData(Representation* src);
    ~Representation() {};
    std::string url_;
    std::string id;
    std::string codecs_;
    std::string codec_private_data_;
    std::string source_url_;
    std::string base_url_;
    uint32_t bandwidth_; // as bit/s
    uint32_t samplingRate_;
    int width_;
    int height_;
    uint32_t fpsRate_, fpsScale_;
    float aspect_;

    uint32_t assured_buffer_duration_{0};
    uint32_t max_buffer_duration_{0};
    //Flags
    static const uint16_t BYTERANGE = 0;
    static const uint16_t INDEXRANGEEXACT = 1;
    static const uint16_t TEMPLATE = 2;
    static const uint16_t TIMELINE = 4;
    static const uint16_t INITIALIZATION = 8;
    static const uint16_t SEGMENTBASE = 16;
    static const uint16_t SUBTITLESTREAM = 32;
    static const uint16_t INCLUDEDSTREAM = 64;
    static const uint16_t URLSEGMENTS = 128;
    static const uint16_t ENABLED = 256;
    static const uint16_t WAITFORSEGMENT = 512;
    static const uint16_t INITIALIZATION_PREFIXED = 1024;
    static const uint16_t DOWNLOADED = 2048;
    static const uint16_t INITIALIZED = 4096;

    uint16_t flags_;
    uint16_t hdcpVersion_;

    uint32_t indexRangeMin_, indexRangeMax_;
    uint8_t channelCount_, nalLengthSize_;
    uint16_t pssh_set_;
    uint32_t expired_segments_;
    ContainerType containerType_;
    SegmentTemplate segtpl_;
    uint64_t startNumber_;
    uint64_t nextPts_;
    //SegmentList
    uint64_t ptsOffset_;
    uint64_t duration_;
    uint32_t timescale_;
    uint32_t timescale_ext_, timescale_int_;
    Segment initialization_;
    SPINCACHE<Segment> segments_;
    std::chrono::time_point<std::chrono::system_clock> repLastUpdated_;
    const Segment *current_segment_;
    const Segment *get_initialization()const { return (flags_ & INITIALIZATION) ? &initialization_ : 0; };
    const Segment* get_next_segment(const Segment* seg) const
    {
      if (!seg || seg == &initialization_)
        return segments_.Get(0);

      size_t nextPos{segments_.GetPosition(seg) + 1};
      if (nextPos == segments_.data.size())
        return nullptr;

      return segments_.Get(nextPos);
    };

    const Segment* get_segment(size_t pos) const { return ~pos ? segments_.Get(pos) : nullptr; };

    const size_t get_segment_pos(const Segment* segment) const
    {
      return segment ? segments_.data.empty() ? 0 : segments_.GetPosition(segment) : ~(size_t)0;
    }

    const uint16_t get_psshset() const
    {
      return pssh_set_;
    }

    const size_t getCurrentSegmentPos() const
    {
      return get_segment_pos(current_segment_);
    };

    const size_t getCurrentSegmentNumber() const
    {
      return current_segment_ ? get_segment_pos(current_segment_) + startNumber_ : ~(size_t)0;
    };

    const size_t getSegmentNumber(const Segment *segment) const
    {
      return segment ? get_segment_pos(segment) + startNumber_ : ~(size_t)0;
    };

    void SetScaling()
    {
      if (!timescale_)
      {
        timescale_ext_ = timescale_int_ = 1;
        return;
      }
      timescale_ext_ = 1000000;
      timescale_int_ = timescale_;
      while (timescale_ext_ > 1)
        if ((timescale_int_ / 10) * 10 == timescale_int_)
        {
          timescale_ext_ /= 10;
          timescale_int_ /= 10;
        }
        else
          break;
    }

    static bool compare(const Representation* a, const Representation *b) { return a->bandwidth_ < b->bandwidth_; };

  }*current_representation_;

  struct AdaptationSet
  {
    AdaptationSet() :type_(NOTYPE), timescale_(0), duration_(0), startPTS_(0), startNumber_(1), impaired_(false), original_(false), default_(false), forced_(false) { language_ = "unk"; };
    ~AdaptationSet() { for (std::vector<Representation* >::const_iterator b(representations_.begin()), e(representations_.end()); b != e; ++b) delete *b; };
    void CopyBasicData(AdaptationSet* src);
    StreamType type_;
    uint32_t timescale_;
    uint64_t duration_;
    uint64_t startPTS_;
    uint64_t startNumber_;
    bool impaired_, original_, default_, forced_;
    std::string language_;
    std::string mimeType_;
    std::string base_url_;
    std::string id_, group_;
    std::string codecs_;
    std::string audio_track_id_;
    std::string name_;
    std::vector<std::string> switching_ids_;
    std::vector<Representation*> representations_;
    SPINCACHE<uint32_t> segment_durations_;
    SegmentTemplate segtpl_;

    const uint32_t get_segment_duration(size_t pos)
    {
      uint32_t* value = segment_durations_.Get(pos);
      if (value)
        return *value;
      return 0;
    };

    static bool compare(const AdaptationSet* a, const AdaptationSet* b)
    {
      if (a->type_ != b->type_)
        return false;
      if (a->default_ != b->default_)
        return a->default_;

      if (a->type_ == AUDIO)
      {
        if (a->audio_track_id_ != b->audio_track_id_)
          return a->audio_track_id_ < b->audio_track_id_;

        if (a->name_ != b->name_)
          return a->name_ < b->name_;

        if (a->impaired_ != b->impaired_)
          return !a->impaired_;
        if (a->original_ != b->original_)
          return a->original_;

        if (a->representations_[0]->codecs_ != b->representations_[0]->codecs_)
          return a->representations_[0]->codecs_ < b->representations_[0]->codecs_;

        if (a->representations_[0]->channelCount_ != b->representations_[0]->channelCount_)
          return a->representations_[0]->channelCount_ < b->representations_[0]->channelCount_;
      }
      else if (a->type_ == SUBTITLE)
      {
        if (a->impaired_ != b->impaired_)
          return !a->impaired_;
        if (a->forced_ != b->forced_)
          return a->forced_;
      }

      return false;
    };

    static bool compareCodecs(const std::string &a, const std::string &b)
    {
      std::string::size_type posa = a.find_first_of('.');
      std::string::size_type posb = a.find_first_of('.');
      if (posa == posb)
        return a.compare(0, posa, b, 0, posb) == 0;
      return false;
    };

    static bool mergeable(const AdaptationSet* a, const AdaptationSet* b)
    {
      if (a->type_ != b->type_)
        return false;

      if (a->type_ == VIDEO)
      {
        if (a->group_ == b->group_
          && std::find(a->switching_ids_.begin(), a->switching_ids_.end(), b->id_) != a->switching_ids_.end()
          && std::find(b->switching_ids_.begin(), b->switching_ids_.end(), a->id_) != b->switching_ids_.end())
        {
          return true;
        }
      }
      else if (a->type_ == AUDIO)
      {
        if (a->timescale_ == b->timescale_
          && a->duration_ == b->duration_
          && a->startPTS_ == b->startPTS_
          && a->startNumber_ == b->startNumber_
          && a->impaired_ == b->impaired_
          && a->original_ == b->original_
          && a->default_ == b->default_
          && a->language_ == b->language_
          && a->mimeType_ == b->mimeType_
          && a->base_url_ == b->base_url_
          && a->audio_track_id_ == b->audio_track_id_
          && a->name_ == b->name_
          && a->id_ == b->id_
          && a->group_ == b->group_
          && compareCodecs(a->codecs_, b->codecs_)
          && a->representations_[0]->channelCount_ == b->representations_[0]->channelCount_
          && compareCodecs(a->representations_[0]->codecs_, b->representations_[0]->codecs_))
        {
          return true;
        }
      }

      return false;
    };
  }*current_adaptationset_;

  struct Period
  {
    struct PSSH
    {
      static const uint32_t MEDIA_VIDEO = 1;
      static const uint32_t MEDIA_AUDIO = 2;

      PSSH() :media_(0), use_count_(0) {};
      bool operator == (const PSSH &other) const { return !use_count_ || (media_ == other.media_ && pssh_ == other.pssh_ && defaultKID_ == other.defaultKID_ && iv == other.iv); };
      std::string pssh_;
      std::string defaultKID_;
      std::string iv;
      uint32_t media_;
      uint32_t use_count_;
      AdaptationSet *adaptation_set_;
    };
    std::vector<PSSH> psshSets_;

    Period() { psshSets_.push_back(PSSH()); };
    ~Period() { for (std::vector<AdaptationSet* >::const_iterator b(adaptationSets_.begin()), e(adaptationSets_.end()); b != e; ++b) delete *b; };
    void CopyBasicData(Period*);
    uint16_t InsertPSSHSet(PSSH* pssh);
    void InsertPSSHSet(uint16_t pssh_set) { ++psshSets_[pssh_set].use_count_; };
    void RemovePSSHSet(uint16_t pssh_set);

    std::vector<AdaptationSet*> adaptationSets_;
    std::string base_url_, id_;
    uint32_t timescale_ = 1000;
    uint32_t sequence_ = 0;
    uint64_t startNumber_ = 1;
    uint64_t start_ = 0;
    uint64_t startPTS_ = 0;
    uint64_t duration_ = 0;
    unsigned int  encryptionState_ = ENCRYTIONSTATE_UNENCRYPTED;
    uint32_t included_types_ = 0;
    bool need_secure_decoder_ = false;
    SPINCACHE<uint32_t> segment_durations_;
    SegmentTemplate segtpl_;
  }*current_period_, *next_period_;

  std::vector<Period*> periods_;
  std::string manifest_url_;
  std::string base_url_;
  std::string effective_url_;
  std::string update_parameter_;
  HTTPRespHeaders m_manifestHeaders;

  /* XML Parsing*/
  XML_Parser parser_;
  uint32_t currentNode_;
  size_t segcount_;
  uint32_t initial_sequence_ = ~0UL;
  uint64_t overallSeconds_, stream_start_, available_time_, base_time_, live_delay_;
  uint64_t minPresentationOffset;
  bool has_timeshift_buffer_, has_overall_seconds_;

  std::string supportedKeySystem_, location_;

  uint8_t adpChannelCount_, adp_pssh_set_;
  int adpwidth_;
  int adpheight_;
  uint32_t adpfpsRate_, adpfpsScale_;
  float adpaspect_;
  ContainerType adpContainerType_;
  bool adp_timelined_, period_timelined_;

  bool current_hasRepURN_, current_hasAdpURN_;
  std::string current_pssh_, current_defaultKID_, current_iv_;
  std::string license_url_;

  std::string strXMLText_;

  AdaptiveTree(const UTILS::PROPERTIES::KodiProperties& properties,
    CHOOSER::IRepresentationChooser* reprChooser);
  virtual ~AdaptiveTree();

  virtual bool open(const std::string& url, const std::string& manifestUpdateParam) = 0;
  virtual bool open(const std::string& url, const std::string& manifestUpdateParam, std::map<std::string, std::string> additionalHeaders) = 0;
  virtual PREPARE_RESULT prepareRepresentation(Period* period,
                                               AdaptationSet* adp,
                                               Representation* rep,
                                               bool update = false)
  {
    return PREPARE_RESULT_OK;
  };
  virtual std::chrono::time_point<std::chrono::system_clock> GetRepLastUpdated(const Representation* rep) { return std::chrono::system_clock::now(); }
  virtual void OnDataArrived(uint64_t segNum, uint16_t psshSet, uint8_t iv[16], const uint8_t *src, uint8_t *dst, size_t dstOffset, size_t dataSize);
  virtual void RefreshSegments(Period* period,
                               AdaptationSet* adp,
                               Representation* rep,
                               StreamType type){};

  bool has_type(StreamType t);
  void FreeSegments(Period* period, Representation* rep);
  uint32_t estimate_segcount(uint64_t duration, uint32_t timescale);
  void SetFragmentDuration(const AdaptationSet* adp, const Representation* rep, size_t pos, uint64_t timestamp, uint32_t fragmentDuration, uint32_t movie_timescale);
  uint16_t insert_psshset(StreamType type, Period* period = nullptr, AdaptationSet* adp = nullptr);

  bool empty(){ return periods_.empty(); };
  AdaptationSet* GetAdaptationSet(unsigned int pos) const
  {
    return current_period_ && pos < current_period_->adaptationSets_.size()
               ? current_period_->adaptationSets_[pos]
               : 0;
  };

  std::string BuildDownloadUrl(const std::string& url) const;

  std::mutex &GetTreeMutex() { return treeMutex_; };
  bool HasUpdateThread() const { return updateThread_ != 0 && has_timeshift_buffer_ && updateInterval_ && !update_parameter_.empty(); };
  void RefreshUpdateThread();
  const std::chrono::time_point<std::chrono::system_clock> GetLastUpdated() const { return lastUpdated_; };

  CHOOSER::IRepresentationChooser* GetRepChooser() { return m_reprChooser; }

  int SecondsSinceRepUpdate(Representation* rep)
  {
    return static_cast<int>(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - GetRepLastUpdated(rep))
      .count());
  }

  virtual AdaptiveTree* Clone() const = 0;

  Settings m_settings;

protected:
  /*!
   * \brief Download a file (At each call feed also the repr. chooser
            to calculate the initial bandwidth).
   * \param url The url of the file to download
   * \param reqHeaders The headers to use in the HTTP request
   * \param data [OUT] Return the HTTP response data
   * \param respHeaders [OUT] Return the HTTP response headers
   * \return True if has success, otherwise false
   */
  virtual bool download(const std::string& url,
                        const std::map<std::string, std::string>& reqHeaders,
                        std::stringstream& data,
                        HTTPRespHeaders& respHeaders);

  bool PreparePaths(const std::string &url);
  void PrepareManifestUrl(const std::string &url, const std::string &manifestUpdateParam);
  void SortTree();

  // Live segment update section
  virtual void StartUpdateThread();
  virtual void RefreshLiveSegments(){};

  uint32_t updateInterval_;
  std::mutex treeMutex_, updateMutex_;
  std::condition_variable updateVar_;
  std::thread *updateThread_;
  std::chrono::time_point<std::chrono::system_clock> lastUpdated_;
  std::map<std::string, std::string> m_streamHeaders;
  const UTILS::PROPERTIES::KodiProperties m_kodiProps;
  CHOOSER::IRepresentationChooser* m_reprChooser{nullptr};

private:
  void SegmentUpdateWorker();
};

} // namespace adaptive
