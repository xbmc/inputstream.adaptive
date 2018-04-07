/*
*      Copyright (C) 2017 peak3d
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

#include "HLSTree.h"
#include <map>
#include <string.h>
#include <thread>

#include "../log.h"
#include "../aes_decrypter.h"
#include "../helpers.h"

using namespace adaptive;

static void parseLine(const std::string &line, size_t offset, std::map<std::string, std::string> &map)
{
  size_t value, end;
  map.clear();
  while (offset < line.size() && (value = line.find('=', offset)) != std::string::npos)
  {
    while (offset < line.size() && line[offset] == ' ')
      ++offset;
    end = value;
    uint8_t inValue(0);
    while (++end < line.size() && ((inValue & 1) || line[end] != ','))
      if (line[end] == '\"') ++inValue;
    map[line.substr(offset, value - offset)] = line.substr(value + (inValue ? 2: 1), end - value - (inValue ? 3 : 1));
    offset = end + 1;
  }
}

static void parseResolution(std::uint16_t &width, std::uint16_t &height, const std::string &val)
{
  std::string::size_type pos(val.find('x'));
  if (pos != std::string::npos)
  {
    width = atoi(val.c_str());
    height = atoi(val.c_str()+pos+1);
  }
}

static std::string getVideoCodec(const std::string &codecs)
{
  if (codecs.empty() || codecs.find("avc1.") != std::string::npos)
    return "h264";
  return "";
}

static std::string getAudioCodec(const std::string &codecs)
{
  if (codecs.find("ec-3") != std::string::npos)
    return "ec-3";
  else if (codecs.find("ac-3") != std::string::npos)
    return "ac-3";
  else
    return  "aac";
}

HLSTree::~HLSTree()
{
  delete m_decrypter;
}

bool HLSTree::open(const std::string &url, const std::string &manifestUpdateParam)
{
  PreparePaths(url, manifestUpdateParam);
  std::stringstream stream;
  if (download(manifest_url_.c_str(), manifest_headers_, &stream))
  {
#if FILEDEBUG
    FILE *f = fopen("inputstream_adaptive_master.m3u8", "w");
    fwrite(m_stream.str().data(), 1, m_stream.str().size(), f);
    fclose(f);
#endif

    std::string line;
    bool startCodeFound = false;

    current_adaptationset_ = nullptr;
    current_representation_ = nullptr;

    periods_.push_back(new Period());
    current_period_ = periods_[0];

    std::map<std::string, std::string> map;

    while (std::getline(stream, line))
    {
      if (!startCodeFound)
      {
        if (line.compare(0, 7, "#EXTM3U") == 0)
          startCodeFound = true;
        continue;
      }

      std::string::size_type sz(line.size());
      while (sz && (line[sz - 1] == '\r' || line[sz - 1] == '\n' || line[sz - 1] == ' ')) --sz;
      line.resize(sz);

      if (line.compare(0, 13, "#EXT-X-MEDIA:") == 0)
      {
        //#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="bipbop_audio",LANGUAGE="eng",NAME="BipBop Audio 2",AUTOSELECT=NO,DEFAULT=NO,URI="alternate_audio_aac_sinewave/prog_index.m3u8"
        parseLine(line, 13, map);

        StreamType type;
        if (map["TYPE"] == "AUDIO")
          type = AUDIO;
        //else if (map["TYPE"] == "SUBTITLES")
        //  type = SUBTITLE;
        else
          continue;

        EXTGROUP &group = m_extGroups[map["GROUP-ID"]];

        AdaptationSet *adp = new AdaptationSet();
        Representation *rep = new Representation();
        adp->repesentations_.push_back(rep);
        group.m_sets.push_back(adp);

        adp->type_ = type;
        adp->language_ =  map["LANGUAGE"];
        adp->timescale_ = 1000000;

        rep->codecs_ = group.m_codec;
        rep->timescale_ = 1000000;
        rep->containerType_ = CONTAINERTYPE_NOTYPE;

        std::map<std::string, std::string>::iterator res;
        if ((res = map.find("URI")) != map.end())
        {
          if (res->second[0] == '/')
            rep->source_url_ = base_domain_ + res->second;
          else if (res->second.find("://", 0) == std::string::npos)
            rep->source_url_ = base_url_ + res->second;
          else
            rep->source_url_ = res->second;
        }
        else
        {
          rep->flags_ = Representation::INCLUDEDSTREAM;
          included_types_ |= 1U << type;
        }

        if ((res = map.find("CHANNELS")) != map.end())
          rep->channelCount_ = atoi(res->second.c_str());
      }
      else if (line.compare(0, 18, "#EXT-X-STREAM-INF:") == 0)
      {
        // TODO: If CODECS value is not present, get StreamReps from stream program section
        //#EXT-X-STREAM-INF:BANDWIDTH=263851,CODECS="mp4a.40.2, avc1.4d400d",RESOLUTION=416x234,AUDIO="bipbop_audio",SUBTITLES="subs"
        parseLine(line, 18, map);

        current_representation_ = nullptr;

        if (map.find("BANDWIDTH") == map.end())
          continue;

        if (!current_adaptationset_)
        {
          current_adaptationset_ = new AdaptationSet();
          current_adaptationset_->type_ = VIDEO;
          current_adaptationset_->timescale_ = 1000000;
          current_period_->adaptationSets_.push_back(current_adaptationset_);
        }
        current_representation_ = new Representation();
        current_adaptationset_->repesentations_.push_back(current_representation_);
        current_representation_->timescale_ = 1000000;
        current_representation_->codecs_ = getVideoCodec(map["CODECS"]);
        current_representation_->bandwidth_ = atoi(map["BANDWIDTH"].c_str());
        current_representation_->containerType_ = CONTAINERTYPE_NOTYPE;

        if (map.find("RESOLUTION") != map.end())
          parseResolution(current_representation_->width_, current_representation_->height_, map["RESOLUTION"]);

        if (map.find("AUDIO") != map.end())
          m_extGroups[map["AUDIO"]].setCodec(getAudioCodec(map["CODECS"]));
        else
        {
          // We assume audio is included
          included_types_ |= 1U << AUDIO;
          m_audioCodec = getAudioCodec(map["CODECS"]);
        }
      }
      else if (!line.empty() && line.compare(0, 1, "#") != 0 && current_representation_)
      {
        if (line[0] == '/')
          current_representation_->source_url_ = base_domain_ + line;
        else if (line.find("://", 0) == std::string::npos)
          current_representation_->source_url_ = base_url_ + line;
        else
          current_representation_->source_url_ = line;

        //Ignore duplicate reps
        for (auto const *rep : current_adaptationset_->repesentations_)
          if (rep != current_representation_ &&  rep->source_url_ == current_representation_->source_url_)
          {
            delete current_representation_;
            current_representation_ = nullptr;
            current_adaptationset_->repesentations_.pop_back();
            break;
          }
      }
    }

    if (current_period_)
    {
      // We may need to create the Default / Dummy audio representation
      if (!m_audioCodec.empty())
      {
        current_adaptationset_ = new AdaptationSet();
        current_adaptationset_->type_ = AUDIO;
        current_adaptationset_->timescale_ = 1000000;
        current_period_->adaptationSets_.push_back(current_adaptationset_);

        current_representation_ = new Representation();
        current_representation_->timescale_ = 1000000;
        current_representation_->codecs_ = m_audioCodec;
        current_representation_->flags_ = Representation::INCLUDEDSTREAM;
        current_adaptationset_->repesentations_.push_back(current_representation_);
      }

      //Register external adaptationsets
      for (const auto &group : m_extGroups)
        for (auto *adp : group.second.m_sets)
          current_period_->adaptationSets_.push_back(adp);
      m_extGroups.clear();

      SortTree();
    }
    // Set Live as default
    has_timeshift_buffer_ = true;
    update_parameter_ = "full";
    return true;
  }
  return false;
}

bool HLSTree::prepareRepresentation(Representation *rep, bool update)
{
  if (!rep->source_url_.empty())
  {
    SPINCACHE<Segment> newSegments;
    unsigned int newStartNumber;
    uint32_t segmentId(rep->getCurrentSegmentNumber());
    std::stringstream stream;

    if (download(rep->source_url_.c_str(), manifest_headers_, &stream, false))
    {
#if FILEDEBUG
      FILE *f = fopen("inputstream_adaptive_sub.m3u8", "w");
      fwrite(m_stream.str().data(), 1, m_stream.str().size(), f);
      fclose(f);
#endif
      bool byteRange(false);
      std::string line;
      std::string base_url;

      std::map<std::string, std::string> map;
      bool startCodeFound(false);
      Segment segment;
      uint64_t pts(0);
      newStartNumber = 0;

      segment.range_begin_ = ~0ULL;
      segment.range_end_ = 0;
      segment.startPTS_ = ~0ULL;
      segment.pssh_set_ = 0;

      std::string::size_type bs = rep->source_url_.rfind('/');
      if (bs != std::string::npos)
        base_url = rep->source_url_.substr(0, bs + 1);

      while (std::getline(stream, line))
      {
        if (!startCodeFound)
        {
          if (line.compare(0, 7, "#EXTM3U") == 0)
            startCodeFound = true;
          continue;
        }

        std::string::size_type sz(line.size());
        while (sz && (line[sz - 1] == '\r' || line[sz - 1] == '\n' || line[sz - 1] == ' ')) --sz;
        line.resize(sz);

        if (line.compare(0, 8, "#EXTINF:") == 0)
        {
          segment.startPTS_ = pts;
          pts += static_cast<uint64_t>(atof(line.c_str() + 8) * rep->timescale_);
        }
        else if (line.compare(0, 17, "#EXT-X-BYTERANGE:") == 0)
        {
          std::string::size_type bs = line.rfind('@');
          if (bs != std::string::npos)
          {
            segment.range_begin_ = atoll(line.c_str() + (bs + 1));
            segment.range_end_ = segment.range_begin_ + atoll(line.c_str() + 17) - 1;
          }
          byteRange = true;
        }
        else if (!line.empty() && line.compare(0, 1, "#") != 0 && ~segment.startPTS_)
        {
          if (rep->containerType_ == CONTAINERTYPE_NOTYPE)
          {
            std::string::size_type paramPos = line.rfind('?');
            std::string::size_type ext = line.rfind('.', paramPos);
            if (ext != std::string::npos)
            {
              if (strncmp(line.c_str() + ext, ".ts", 3) == 0)
                rep->containerType_ = CONTAINERTYPE_TS;
              else if (strncmp(line.c_str() + ext, ".aac", 4) == 0)
                rep->containerType_ = CONTAINERTYPE_ADTS;
              else if (strncmp(line.c_str() + ext, ".mp4", 4) == 0)
                rep->containerType_ = CONTAINERTYPE_MP4;
              else
              {
                rep->containerType_ = CONTAINERTYPE_INVALID;
                continue;
              }
            }
            else
              //Fallback, assume .ts
              rep->containerType_ = CONTAINERTYPE_TS;
          }

          if (!byteRange || rep->url_.empty())
          {
            std::string url;
            if (line[0] == '/')
              url = base_domain_ + line;
            else if (line.find("://", 0) == std::string::npos)
              url = base_url + line;
            else
              url = line;
            if (!byteRange)
            {
              segment.url = new char[url.size() + 1];
              memcpy((char*)segment.url, url.c_str(), url.size() + 1);
            }
            else
              rep->url_ = url;
          }
          newSegments.data.push_back(segment);
          segment.startPTS_ = ~0ULL;
        }
        else if (line.compare(0, 22, "#EXT-X-MEDIA-SEQUENCE:") == 0)
        {
          newStartNumber = atol(line.c_str() + 22);
        }
        else if (line.compare(0, 21, "#EXT-X-PLAYLIST-TYPE:") == 0)
        {
          if (strcmp(line.c_str() + 21, "VOD") == 0)
          {
            m_refreshPlayList = false;
            has_timeshift_buffer_ = false;
          }
        }
        else if (line.compare(0, 22, "#EXT-X-TARGETDURATION:") == 0)
        {
          uint32_t newInterval = atoi(line.c_str() + 22) * 1500;
          if (newInterval < updateInterval_)
            updateInterval_ = newInterval;
        }
        else if (line.compare(0, 11, "#EXT-X-KEY:") == 0)
        {
          if (!rep->pssh_set_)
          {
            parseLine(line, 11, map);
            if (map["METHOD"] != "NONE")
            {
              if (map["METHOD"] != "AES-128")
              {
                Log(LOGLEVEL_ERROR, "Unsupported encryption method: ", map["METHOD"].c_str());
                return false;
              }
              if (map["URI"].empty())
              {
                Log(LOGLEVEL_ERROR, "Unsupported encryption method: ", map["METHOD"].c_str());
                return false;
              }
              current_pssh_ = map["URI"];
              if (current_pssh_[0] == '/')
                current_pssh_ = base_domain_ + current_pssh_;
              else if (current_pssh_.find("://", 0) == std::string::npos)
                current_pssh_ = base_url + current_pssh_;

              current_iv_ = m_decrypter->convertIV(map["IV"]);
              segment.pssh_set_ = insert_psshset(NOTYPE);
            }
          }
        }
        else if (line.compare(0, 14, "#EXT-X-ENDLIST") == 0)
        {
          m_refreshPlayList = false;
          has_timeshift_buffer_ = false;
        }
      }

      overallSeconds_ = newSegments[0] ? (pts - newSegments[0]->startPTS_) / rep->timescale_ : 0;

      if (!byteRange)
        rep->flags_ |= Representation::URLSEGMENTS;

      // Insert Initialization Segment
      if (rep->containerType_ == CONTAINERTYPE_MP4 && byteRange && newSegments.data[0].range_begin_ > 0)
      {
        rep->flags_ |= Representation::INITIALIZATION;
        rep->initialization_.range_begin_ = 0;
        rep->initialization_.range_end_ = newSegments.data[0].range_begin_ - 1;
        rep->initialization_.pssh_set_ = 0;
      }
    }

    FreeSegments(rep);

    if (newSegments.data.empty())
    {
      rep->source_url_.clear(); // disable this segment
      return false;
    }

    rep->segments_.swap(newSegments);
    rep->startNumber_ = newStartNumber;

    if (update)
    {
      if (!segmentId || segmentId < rep->startNumber_)
        rep->current_segment_ = nullptr;
      else
      {
        if (segmentId >= rep->startNumber_ + rep->segments_.size())
          segmentId = rep->startNumber_ + rep->segments_.size() - 1;
        rep->current_segment_ = rep->get_segment(segmentId - rep->startNumber_);
      }
      if ((rep->flags_ & Representation::WAITFORSEGMENT) && rep->get_next_segment(rep->current_segment_))
        rep->flags_ &= ~Representation::WAITFORSEGMENT;
    }
    else
      StartUpdateThread();

    return true;
  }
  return false;
};

bool HLSTree::write_data(void *buffer, size_t buffer_size, void *opaque)
{
  static_cast<std::stringstream*>(opaque)->write(static_cast<const char*>(buffer), buffer_size);
  return true;
}

void HLSTree::OnDataArrived(unsigned int segNum, uint16_t psshSet, uint8_t iv[16], const uint8_t *src, uint8_t *dst, size_t dstOffset, size_t dataSize)
{
  if (psshSet)
  {
    std::lock_guard<std::mutex> lck(treeMutex_);

    PSSH &pssh(psshSets_[psshSet]);
    //Encrypted media, decrypt it
    if (pssh.defaultKID_.empty())
    {
      //First look if we already have this URL resolved
      for (std::vector<PSSH>::const_iterator b(psshSets_.begin()), e(psshSets_.end());b != e; ++b)
        if (b->pssh_ == pssh.pssh_ && !b->defaultKID_.empty())
        {
          pssh.defaultKID_ = b->defaultKID_;
          break;
        }
      if (pssh.defaultKID_.empty())
      {
      RETRY:
        std::stringstream stream;
        std::map<std::string, std::string> headers;
        std::vector<std::string> keyParts(split(m_decrypter->getLicenseKey(), '|'));
        std::string url = pssh.pssh_.c_str();

        if (keyParts.size() > 0 && !keyParts[0].empty())
        {
          if (url.find_first_of('?') == std::string::npos)
            url += "?";
          else
            url += "&";
          url += keyParts[0];
        }
        if (keyParts.size() > 1)
          parseheader(headers, keyParts[1].c_str());
        if (download(url.c_str(), headers, &stream, false))
        {
          pssh.defaultKID_ = stream.str();
        }
        else if (pssh.defaultKID_ != "0")
        {
          pssh.defaultKID_ = "0";
          if (keyParts.size() >= 5 && !keyParts[4].empty() && m_decrypter->RenewLicense(keyParts[4]))
            goto RETRY;
        }
      }
    }
    if (pssh.defaultKID_ == "0")
    {
      memset(dst + dstOffset, 0, dataSize);
      return;
    }
    else if (!dstOffset)
    {
      if (pssh.iv.empty())
        m_decrypter->ivFromSequence(iv, segNum);
      else
      {
        memset(iv, 0, 16);
        memcpy(iv, pssh.iv.data(), pssh.iv.size() < 16 ? pssh.iv.size() : 16);
      }
    }
    m_decrypter->decrypt(reinterpret_cast<const uint8_t*>(pssh.defaultKID_.data()), iv, src, dst + dstOffset, dataSize);
    if(dataSize >= 16)
      memcpy(iv, src + (dataSize - 16), 16);
  }
  else
    AdaptiveTree::OnDataArrived(segNum, psshSet, iv, src, dst, dstOffset, dataSize);
}

//Called each time before we switch to a new segment
void HLSTree::RefreshSegments(Representation *rep, StreamType type)
{
  if (m_refreshPlayList)
  {
    RefreshUpdateThread();
    prepareRepresentation(rep, true);
  }
}

//Called form update-thread
void HLSTree::RefreshSegments()
{
  if (m_refreshPlayList)
  {
    for (std::vector<Period*>::const_iterator bp(periods_.begin()), ep(periods_.end()); bp != ep; ++bp)
      for (std::vector<AdaptationSet*>::const_iterator ba((*bp)->adaptationSets_.begin()), ea((*bp)->adaptationSets_.end()); ba != ea; ++ba)
        for (std::vector<Representation*>::iterator br((*ba)->repesentations_.begin()), er((*ba)->repesentations_.end()); br != er; ++br)
          if ((*br)->flags_ & Representation::ENABLED)
            prepareRepresentation((*br), true);
  }
}
