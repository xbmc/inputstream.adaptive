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
  if (codecs.find("avc1.") != std::string::npos)
    return "h264";
  return "";
}

static std::string getAudioCodec(const std::string &codecs)
{
  if (codecs.find("mp4a.40.34") != std::string::npos)
    return  "ac-3";
  else
    return codecs.find("mp4a.") != std::string::npos ? "aac" : "";
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

        std::map<std::string, std::string>::iterator res;
        if ((res = map.find("URI")) != map.end())
        {
          if (res->second.find("://", 0, 8) == std::string::npos)
            rep->url_ = base_url_ + res->second;
          else
            rep->url_ = res->second;
        }
        else
          rep->flags_ = Representation::INCLUDEDSTREAM;

        if ((res = map.find("CHANNELS")) != map.end())
          rep->channelCount_ = atoi(res->second.c_str());
      }
      else if (line.compare(0, 18, "#EXT-X-STREAM-INF:") == 0)
      {
        //#EXT-X-STREAM-INF:BANDWIDTH=263851,CODECS="mp4a.40.2, avc1.4d400d",RESOLUTION=416x234,AUDIO="bipbop_audio",SUBTITLES="subs"
        parseLine(line, 18, map);

        current_representation_ = nullptr;

        if (map.find("CODECS") == map.end() || map.find("BANDWIDTH") == map.end() || map.find("RESOLUTION") == map.end())
          continue;

        std::string videoCodec = getVideoCodec(map["CODECS"]);

        if (videoCodec.empty())
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
        current_representation_->codecs_ = videoCodec;
        current_representation_->bandwidth_ = atoi(map["BANDWIDTH"].c_str());
        parseResolution(current_representation_->width_, current_representation_->height_, map["RESOLUTION"]);

        if (map.find("AUDIO") != map.end())
          m_extGroups[map["AUDIO"]].setCodec(getAudioCodec(map["CODECS"]));
        else
          m_audioCodec = getAudioCodec(map["CODECS"]);
      }
      else if (!line.empty() && line.compare(0, 1, "#") != 0 && current_representation_)
      {
        if (line.find("://", 0, 8) == std::string::npos)
          current_representation_->url_ = base_url_ + line;
        else
          current_representation_->url_ = line;

        //Ignore duplicate reps
        bool duplicate(false);
        for (auto const *rep : current_adaptationset_->repesentations_)
          if (rep != current_representation_ &&  rep->url_ == current_representation_->url_)
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
    return true;
  }
  return false;
}

bool HLSTree::prepareRepresentation(Representation *rep)
{
  if (rep->segments_.data.empty() && !rep->url_.empty())
  {
    m_stream.str().clear();
    m_stream.clear();

    std::string line;
    std::map<std::string, std::string> map;
    bool startCodeFound(false);
    Segment segment;
    segment.range_begin_ = segment.range_end_ = 0;
    uint64_t pts(0);

    if (download(rep->url_.c_str(), manifest_headers_))
    {
      std::string base_url;
      std::string::size_type bs = rep->url_.rfind('/');
      if (bs != std::string::npos)
        base_url = rep->url_.substr(0, bs + 1);
      rep->url_.clear();

      while (std::getline(m_stream, line))
      {
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
        }
        else if (!line.empty() && line.compare(0, 1, "#") != 0 && ~segment.startPTS_)
        {
          std::string::size_type ext = line.rfind('.');
          if (ext != std::string::npos)
          {
            if (strcmp(line.c_str() + ext, ".ts") == 0)
              rep->containerType_ = CONTAINERTYPE_TS;
            else if (strcmp(line.c_str() + ext, ".mp4") == 0)
              rep->containerType_ = CONTAINERTYPE_MP4;
            else
            {
              rep->containerType_ = CONTAINERTYPE_NOTYPE;
              continue;
            }
          }

          std::string url(base_url + line);
          if (rep->url_.empty())
            rep->url_ = url;
          if (rep->url_ == url)
            rep->segments_.data.push_back(segment);
          segment.startPTS_ = ~0ULL;
        }
        else if (line.compare(0, 22, "#EXT-X-MEDIA-SEQUENCE:") == 0)
        {
          rep->id = line.c_str() + 22;
        }
        else if (line.compare(0, 21, "#EXT-X-PLAYLIST-TYPE:") == 0)
        {
        }
      }
    }
    overallSeconds_ = static_cast<double>(pts) / rep->timescale_;
  }
  return !rep->segments_.data.empty();
};


bool HLSTree::write_data(void *buffer, size_t buffer_size)
{
  m_stream.write(static_cast<const char*>(buffer), buffer_size);
  return true;
}