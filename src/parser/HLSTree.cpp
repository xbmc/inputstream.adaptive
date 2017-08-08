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
#include "../log.h"
#include "../aes_decrypter.h"

using namespace adaptive;

static void parseLine(const std::string &line, size_t offset, std::map<std::string, std::string> &map)
{
  size_t value, end;
  map.clear();
  while (offset < line.size() && (value = line.find('=', offset)) != std::string::npos)
  {
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
  if (codecs.find("mp4a.40.34") != std::string::npos)
    return  "ac-3";
  else
    return codecs.empty() || codecs.find("mp4a.") != std::string::npos ? "aac" : "";
}

HLSTree::~HLSTree()
{
  delete m_decrypter;
}

void HLSTree::ClearStream()
{
  m_stream.str("");
  m_stream.clear();
}

bool HLSTree::open(const char *url)
{
  if (download(url, manifest_headers_))
  {
    std::string line;
    bool startCodeFound = false;

    current_adaptationset_ = nullptr;
    current_representation_ = nullptr;

    periods_.push_back(new Period());
    current_period_ = periods_[0];

    std::map<std::string, std::string> map;

    while (std::getline(m_stream, line))
    {
      if (!startCodeFound && line.compare(0, 7, "#EXTM3U") == 0)
        startCodeFound = true;
      else if (!startCodeFound)
        continue;

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
          if (res->second.find("://", 0) == std::string::npos)
            rep->source_url_ = base_url_ + res->second;
          else if (res->second[0] == '/')
            rep->source_url_ = base_domain_ + res->second;
          else
            rep->source_url_ = res->second;
        }
        else
          rep->flags_ = Representation::INCLUDEDSTREAM;

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
          m_audioCodec = getAudioCodec(map["CODECS"]);
      }
      else if (!line.empty() && line.compare(0, 1, "#") != 0 && current_representation_)
      {
        if (line.find("://", 0) == std::string::npos)
          current_representation_->source_url_ = base_url_ + line;
        else if (line[0] == '/')
          current_representation_->source_url_ = base_domain_ + line;
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

      SortRepresentations();
    }
    // Set Live as default
    has_timeshift_buffer_ = true;
    return true;
  }
  return false;
}

bool HLSTree::prepareRepresentation(Representation *rep, uint64_t segmentId)
{
  if ((rep->segments_.data.empty() || segmentId) && !rep->source_url_.empty())
  {
    ClearStream();

    if (download(rep->source_url_.c_str(), manifest_headers_))
    {
      bool byteRange(false);
      std::string line;
      std::string base_url;

      std::map<std::string, std::string> map;
      bool startCodeFound(false);
      Segment segment;
      uint64_t pts(rep->nextPTS_), segmentBaseId(0);
      size_t freeSegments(0);
      std::string::size_type segIdxPos(std::string::npos);

      segment.range_begin_ = ~0ULL;
      segment.range_end_ = 0;
      segment.startPTS_ = ~0ULL;

      std::string::size_type bs = rep->source_url_.rfind('/');
      if (bs != std::string::npos)
        base_url = rep->source_url_.substr(0, bs + 1);

      while (std::getline(m_stream, line))
      {
        std::string::size_type sz(line.size());
        while (sz && (line[sz - 1] == '\r' || line[sz - 1] == '\n')) --sz;
        line.resize(sz);

        if (!startCodeFound && line.compare(0, 7, "#EXTM3U") == 0)
          startCodeFound = true;
        else if (!startCodeFound)
          continue;

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
            std::string::size_type ext = line.rfind('.');
            if (ext != std::string::npos)
            {
              if (strncmp(line.c_str() + ext, ".ts", 3) == 0)
                rep->containerType_ = CONTAINERTYPE_TS;
              else if (strncmp(line.c_str() + ext, ".mp4", 4) == 0)
                rep->containerType_ = CONTAINERTYPE_MP4;
              else
              {
                rep->containerType_ = CONTAINERTYPE_NOTYPE;
                continue;
              }
            }
          }

          if (!byteRange || rep->url_.empty())
          {
            std::string url;
            if (line.find("://", 0) == std::string::npos)
              url = base_url + line;
            else if (line[0] == '/')
              url = base_domain_ + line;
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
          if (!~rep->segmentBaseId_)
          {
            rep->segments_.data.push_back(segment);
            rep->nextPTS_ = pts;
          }
          else if (segmentBaseId)
          {
            if (segmentBaseId >= segmentId && freeSegments > 0)
            {
              Log(LOGLEVEL_DEBUG, "Inserting segment: :%llu", segmentBaseId);
              if (byteRange)
                delete rep->segments_[0]->url;
              rep->segments_.insert(segment);
              ++rep->segmentBaseId_;
              --freeSegments;
              rep->nextPTS_ = pts;
            }
            else if (byteRange)
              delete segment.url;
            ++segmentBaseId;
          }
          else if (byteRange)
            delete segment.url;
          segment.startPTS_ = ~0ULL;
        }
        else if (!segmentBaseId && line.compare(0, 22, "#EXT-X-MEDIA-SEQUENCE:") == 0)
        {
          segmentBaseId = atoll(line.c_str() + 22);
          Log(LOGLEVEL_DEBUG, "old base :%llu, new base:%llu, current:%llu", rep->segmentBaseId_, segmentBaseId, segmentId);
          if (segmentBaseId == rep->segmentBaseId_)
            return true; //Nothing to do
          else if (!~rep->segmentBaseId_)
            continue;
          //calculate first and last segment we have to replace
          if (segmentBaseId > rep->segmentBaseId_ + rep->segments_.data.size()) //we have lost our window / game over
            return false;
          freeSegments = static_cast<size_t>(segmentId - rep->segmentBaseId_); // Number of slots to fill
          segmentId += rep->segments_.data.size() - freeSegments; //First segmentId to be inserted
        }
        else if (line.compare(0, 21, "#EXT-X-PLAYLIST-TYPE:") == 0)
        {
          if (strcmp(line.c_str() + 21, "VOD") == 0)
          {
            m_refreshPlayList = false;
            has_timeshift_buffer_ = false;
          }
        }
        else if (line.compare(0, 11, "#EXT-X-KEY:") == 0)
        {
          if (!rep->pssh_set_)
          {
            parseLine(line, 11, map);
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
            if (current_pssh_.find("://", 0) == std::string::npos)
              current_pssh_ = base_url + current_pssh_;
            else if (current_pssh_[0] == '/')
              current_pssh_ = base_domain_ + current_pssh_;

            current_iv_ = m_decrypter->convertIV(map["IV"]);
            rep->pssh_set_ = insert_psshset(NOTYPE);
          }
        }
        else if (line.compare(0, 14, "#EXT-X-ENDLIST") == 0)
        {
          m_refreshPlayList = false;
        }
      }

      overallSeconds_ = rep->segments_[0] ? (pts - rep->segments_[0]->startPTS_) / rep->timescale_ : 0;

      if (!~rep->segmentBaseId_)
        rep->segmentBaseId_ = segmentBaseId;

      if (!byteRange)
        rep->flags_ |= Representation::URLSEGMENTS;

      // Insert Initialization Segment
      if (rep->containerType_ == CONTAINERTYPE_MP4 && byteRange && rep->segments_.data[0].range_begin_ > 0)
      {
        rep->flags_ |= Representation::INITIALIZATION;
        rep->initialization_.range_begin_ = 0;
        rep->initialization_.range_end_ = rep->segments_.data[0].range_begin_ - 1;
      }
    }
  }

  if (rep->segments_.data.empty())
  {
    rep->source_url_.clear(); // disable this segment
    return false;
  }
  return true;
};

bool HLSTree::write_data(void *buffer, size_t buffer_size)
{
  m_stream.write(static_cast<const char*>(buffer), buffer_size);
  return true;
}

// TODO Decryption if required
void HLSTree::OnSegmentDownloaded(Representation *rep, const Segment *seg, std::string &data)
{
  if (rep->pssh_set_)
  {
    PSSH &pssh(psshSets_[rep->pssh_set_]);
    //Encrypted media, decrypt it
    if (pssh.defaultKID_.empty())
    {
      ClearStream();
      if (download(pssh.pssh_.c_str(), manifest_headers_))
      {
        pssh.defaultKID_ =  m_stream.str();
      }
    }

    uint8_t iv[16];
    if (pssh.iv.empty())
      m_decrypter->ivFromSequence(iv, rep->segmentBaseId_ + rep->segments_.pos(seg));
    else
      memcpy(iv, pssh.iv.data(), 16);

    m_decrypter->decrypt(reinterpret_cast<const uint8_t*>(pssh.defaultKID_.data()), iv, data);
  }

  if (m_refreshPlayList && rep->containerType_ == CONTAINERTYPE_TS && rep->segments_.pos(seg))
    prepareRepresentation(rep, rep->segmentBaseId_ + rep->segments_.pos(seg));
}
