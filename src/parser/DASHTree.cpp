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

#include <string>
#include <cstring>
#include <time.h>
#include <float.h>
#include <thread>

#include "DASHTree.h"
#include "../oscompat.h"
#include "../helpers.h"
#include "../log.h"

using namespace adaptive;

const char* TRANSLANG[370] = {
  "aa", "aar",
  "ab", "abk",
  "ae", "ave",
  "af", "afr",
  "ak", "aka",
  "am", "amh",
  "an", "arg",
  "ar", "ara",
  "as", "asm",
  "av", "ava",
  "ay", "aym",
  "az", "aze",
  "ba", "bak",
  "be", "bel",
  "bg", "bul",
  "bi", "bis",
  "bm", "bam",
  "bn", "ben",
  "bo", "bod",
  "br", "bre",
  "bs", "bos",
  "ca", "cat",
  "ce", "che",
  "ch", "cha",
  "co", "cos",
  "cr", "cre",
  "cs", "ces",
  "cu", "chu",
  "cv", "chv",
  "cy", "cym",
  "da", "dan",
  "de", "deu",
  "dv", "div",
  "dz", "dzo",
  "ee", "ewe",
  "el", "ell",
  "en", "eng",
  "eo", "epo",
  "es", "spa",
  "et", "est",
  "eu", "eus",
  "fa", "fas",
  "ff", "ful",
  "fi", "fin",
  "fj", "fij",
  "fo", "fao",
  "fr", "fra",
  "fy", "fry",
  "ga", "gle",
  "gd", "gla",
  "gl", "glg",
  "gn", "grn",
  "gu", "guj",
  "gv", "glv",
  "ha", "hau",
  "he", "heb",
  "hi", "hin",
  "ho", "hmo",
  "hr", "hrv",
  "ht", "hat",
  "hu", "hun",
  "hy", "hye",
  "hz", "her",
  "ia", "ina",
  "id", "ind",
  "ie", "ile",
  "ig", "ibo",
  "ii", "iii",
  "ik", "ipk",
  "io", "ido",
  "is", "isl",
  "it", "ita",
  "iu", "iku",
  "ja", "jpn",
  "jv", "jav",
  "ka", "kat",
  "kg", "kon",
  "ki", "kik",
  "kj", "kua",
  "kk", "kaz",
  "kl", "kal",
  "km", "khm",
  "kn", "kan",
  "ko", "kor",
  "kr", "kau",
  "ks", "kas",
  "ku", "kur",
  "kv", "kom",
  "kw", "cor",
  "ky", "kir",
  "la", "lat",
  "lb", "ltz",
  "lg", "lug",
  "li", "lim",
  "ln", "lin",
  "lo", "lao",
  "lt", "lit",
  "lu", "lub",
  "lv", "lav",
  "mg", "mlg",
  "mh", "mah",
  "mi", "mri",
  "mk", "mkd",
  "ml", "mal",
  "mn", "mon",
  "mr", "mar",
  "ms", "msa",
  "mt", "mlt",
  "my", "mya",
  "na", "nau",
  "nb", "nob",
  "nd", "nde",
  "ne", "nep",
  "ng", "ndo",
  "nl", "nld",
  "nn", "nno",
  "no", "nor",
  "nr", "nbl",
  "nv", "nav",
  "ny", "nya",
  "oc", "oci",
  "oj", "oji",
  "om", "orm",
  "or", "ori",
  "os", "oss",
  "pa", "pan",
  "pi", "pli",
  "pl", "pol",
  "ps", "pus",
  "pt", "por",
  "qu", "que",
  "rm", "roh",
  "rn", "run",
  "ro", "ron",
  "ru", "rus",
  "rw", "kin",
  "sa", "san",
  "sc", "srd",
  "sd", "snd",
  "se", "sme",
  "sg", "sag",
  "sh", "hbs",
  "si", "sin",
  "sk", "slk",
  "sl", "slv",
  "sm", "smo",
  "sn", "sna",
  "so", "som",
  "sq", "sqi",
  "sr", "srp",
  "ss", "ssw",
  "st", "sot",
  "su", "sun",
  "sv", "swe",
  "sw", "swa",
  "ta", "tam",
  "te", "tel",
  "tg", "tgk",
  "th", "tha",
  "ti", "tir",
  "tk", "tuk",
  "tl", "tgl",
  "tn", "tsn",
  "to", "ton",
  "tr", "tur",
  "ts", "tso",
  "tt", "tat",
  "tw", "twi",
  "ty", "tah",
  "ug", "uig",
  "uk", "ukr",
  "ur", "urd",
  "uz", "uzb",
  "ve", "ven",
  "vi", "vie",
  "vo", "vol",
  "wa", "wln",
  "wo", "wol",
  "xh", "xho",
  "yi", "yid",
  "yo", "yor",
  "za", "zha",
  "zh", "zho",
  "zu", "zul"
};

enum
{
  MPDNODE_MPD = 1 << 0,
  MPDNODE_PERIOD = 1 << 1,
  MPDNODE_ADAPTIONSET = 1 << 2,
  MPDNODE_CONTENTPROTECTION = 1 << 3,
  MPDNODE_REPRESENTATION = 1 << 4,
  MPDNODE_BASEURL = 1 << 5,
  MPDNODE_SEGMENTLIST = 1 << 6,
  MPDNODE_INITIALIZATION = 1 << 7,
  MPDNODE_SEGMENTURL = 1 << 8,
  MPDNODE_SEGMENTDURATIONS = 1 << 9,
  MPDNODE_S = 1 << 11,
  MPDNODE_PSSH = 1 << 12,
  MPDNODE_SEGMENTTEMPLATE = 1 << 13,
  MPDNODE_SEGMENTTIMELINE = 1 << 14
};

static const char* ltranslate(const char * in)
{
  //TODO: qfind or stop if >
  if (strlen(in) == 2)
  {
    for (unsigned int i(0); i < 185; ++i)
      if (strcmp(in, TRANSLANG[i * 2]) == 0)
        return TRANSLANG[i * 2 + 1];
  }
  else if (strlen(in) == 3)
    return in;
  return "unk";
}

DASHTree::DASHTree()
{
}

static uint8_t GetChannels(const char **attr)
{
  const char *schemeIdUri(0), *value(0);

  for (; *attr;)
  {
    if (strcmp((const char*)*attr, "schemeIdUri") == 0)
      schemeIdUri = (const char*)*(attr + 1);
    else if (strcmp((const char*)*attr, "value") == 0)
      value = (const char*)*(attr + 1);
    attr += 2;
  }
  if (schemeIdUri && value)
  {
    if (strcmp(schemeIdUri, "urn:mpeg:dash:23003:3:audio_channel_configuration:2011") == 0)
      return atoi(value);
    else if (strcmp(schemeIdUri, "urn:dolby:dash:audio_channel_configuration:2011") == 0)
    {
      if (strcmp(value, "F801") == 0)
        return 6;
      else if (strcmp(value, "FE01") == 0)
        return 8;
    }
  }
  return 0;
}

static unsigned int ParseSegmentTemplate(const char **attr, std::string baseURL, DASHTree::SegmentTemplate &tpl)
{
  unsigned int startNumber(1);
  for (; *attr;)
  {
    if (strcmp((const char*)*attr, "timescale") == 0)
      tpl.timescale = atoi((const char*)*(attr + 1));
    else if (strcmp((const char*)*attr, "duration") == 0)
      tpl.duration = atoi((const char*)*(attr + 1));
    else if (strcmp((const char*)*attr, "media") == 0)
      tpl.media = (const char*)*(attr + 1);
    else if (strcmp((const char*)*attr, "startNumber") == 0)
      startNumber = atoi((const char*)*(attr + 1));
    else if (strcmp((const char*)*attr, "initialization") == 0)
      tpl.initialization = (const char*)*(attr + 1);
    attr += 2;
  }

  if (tpl.media.compare(0, 7, "http://") != 0
  && tpl.media.compare(0, 8, "https://") != 0)
    tpl.media = baseURL + tpl.media;
  return startNumber;
}

static time_t getTime(const char* timeStr)
{
  int year, mon, day, hour, minu, sec;
  if (sscanf(timeStr, "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &hour, &minu, &sec) == 6)
  {
    struct tm tmd;

    memset(&tmd,0,sizeof(tmd));
    tmd.tm_year = year - 1900;
    tmd.tm_mon = mon - 1;
    tmd.tm_mday = day;
    tmd.tm_hour = hour;
    tmd.tm_min = minu;
    tmd.tm_sec = sec;
    return _mkgmtime(&tmd);
  }
  return ~0;
}

static void AddDuration(const char* dur, uint64_t& retVal, uint32_t scale)
{
  if (dur && *dur++ == 'P' && *dur++ == 'T')
  {
    const char *next = strchr(dur, 'H');
    if (next) {
      retVal += static_cast<uint64_t>(atof(dur) * 3600 * scale);
      dur = next + 1;
    }
    next = strchr(dur, 'M');
    if (next) {
      retVal += static_cast<uint64_t>(atof(dur) * 60 * scale);
      dur = next + 1;
    }
    next = strchr(dur, 'S');
    if (next)
      retVal += static_cast<uint64_t>(atof(dur) * scale);
  }
}

bool ParseContentProtection(const char **attr, DASHTree *dash)
{
  dash->strXMLText_.clear();
  dash->encryptionState_ |= DASHTree::ENCRYTIONSTATE_ENCRYPTED;
  bool urnFound(false), mpdFound(false);
  const char *defaultKID(0);
  for (; *attr;)
  {
    if (strcmp((const char*)*attr, "schemeIdUri") == 0)
    {
      if (strcmp((const char*)*(attr + 1), "urn:mpeg:dash:mp4protection:2011") == 0)
        mpdFound = true;
      else
      {
        urnFound = stricmp(dash->supportedKeySystem_.c_str(), (const char*)*(attr + 1)) == 0;
        break;
      }
    }
    else if (strcmp((const char*)*attr, "cenc:default_KID") == 0)
      defaultKID = (const char*)*(attr + 1);
    attr += 2;
  }
  if (urnFound)
  {
    dash->currentNode_ |= MPDNODE_CONTENTPROTECTION;
    dash->encryptionState_ |= DASHTree::ENCRYTIONSTATE_SUPPORTED;
    return true;
  }
  else if (mpdFound && defaultKID && strlen(defaultKID) == 36)
  {
    dash->current_defaultKID_.resize(16);
    for (unsigned int i(0); i < 16; ++i)
    {
      if (i == 4 || i == 6 || i == 8 || i == 10)
        ++defaultKID;
      dash->current_defaultKID_[i] = HexNibble(*defaultKID) << 4; ++defaultKID;
      dash->current_defaultKID_[i] |= HexNibble(*defaultKID); ++defaultKID;
    }
  }
  // Return if we have URN or not
  return !mpdFound;
}

/*----------------------------------------------------------------------
|   expat start
+---------------------------------------------------------------------*/

static void ReplacePlaceHolders(std::string &rep, const std::string &id, uint32_t bandwidth)
{
  std::string::size_type repPos = rep.find("$RepresentationID$");
  if (repPos != std::string::npos)
    rep.replace(repPos, 18, id);

  repPos = rep.find("$Bandwidth$");
  if (repPos != std::string::npos)
  {
    char bw[32];
    sprintf(bw, "%u", bandwidth);
    rep.replace(repPos, 11, bw);
  }
}

static void XMLCALL
start(void *data, const char *el, const char **attr)
{
  DASHTree *dash(reinterpret_cast<DASHTree*>(data));

  if (dash->currentNode_ & MPDNODE_MPD)
  {
    if (dash->currentNode_ & MPDNODE_PERIOD)
    {
      if (dash->currentNode_ & MPDNODE_ADAPTIONSET)
      {
        if (dash->currentNode_ & MPDNODE_REPRESENTATION)
        {
          if (dash->currentNode_ & MPDNODE_BASEURL)
          {
          }
          else if (dash->currentNode_ & MPDNODE_SEGMENTLIST)
          {
            DASHTree::Segment seg;
            seg.pssh_set_ = 0;
            if (strcmp(el, "SegmentURL") == 0)
            {
              for (; *attr;)
              {
                if (strcmp((const char*)*attr, "mediaRange") == 0)
                {
                  seg.SetRange((const char*)*(attr + 1));
                  break;
                }
                else if (strcmp((const char*)*attr, "media") == 0)
                {
                  dash->current_representation_->flags_ |= DASHTree::Representation::URLSEGMENTS;
                  size_t sz(strlen((const char*)*(attr + 1)) + 1);
                  seg.url = new char[sz];
                  memcpy((char*)seg.url, (const char*)*(attr + 1), sz);
                }
                attr += 2;
              }
              dash->current_representation_->segments_.data.push_back(seg);
            }
            else if (strcmp(el, "Initialization") == 0)
            {
              for (; *attr;)
              {
                if (strcmp((const char*)*attr, "range") == 0)
                {
                  seg.SetRange((const char*)*(attr + 1));
                  break;
                }
                else if (strcmp((const char*)*attr, "sourceURL") == 0)
                {
                  seg.range_begin_ = ~0ULL;
                  size_t sz(strlen((const char*)*(attr + 1)) + 1);
                  seg.url = new char[sz];
                  memcpy(const_cast<char*>(seg.url), (const char*)*(attr + 1), sz);
                  dash->current_representation_->flags_ |= DASHTree::Representation::URLSEGMENTS;
                }
                attr += 2;
              }
              dash->current_representation_->flags_ |= DASHTree::Representation::INITIALIZATION;
              dash->current_representation_->initialization_ = seg;
            }
            else
              return;
          }
          else if (dash->currentNode_ & MPDNODE_SEGMENTTEMPLATE)
          {
            if (dash->currentNode_ & MPDNODE_SEGMENTTIMELINE)
            {
              // <S t="3600" d="900000" r="2398"/>
              unsigned int d(0), r(1);
              static uint64_t t(0);

              for (; *attr;)
              {
                if (strcmp((const char*)*attr, "t") == 0)
                  t = atoll((const char*)*(attr + 1));
                else if (strcmp((const char*)*attr, "d") == 0)
                  d = atoi((const char*)*(attr + 1));
                if (strcmp((const char*)*attr, "r") == 0)
                  r = atoi((const char*)*(attr + 1))+1;
                attr += 2;
              }
              if (d && r)
              {
                DASHTree::Segment s;
                if (dash->current_representation_->segments_.data.empty())
                {
                  if (dash->current_representation_->segtpl_.duration && dash->current_representation_->segtpl_.timescale)
                    dash->current_representation_->segments_.data.reserve((unsigned int)((double)dash->overallSeconds_ / (((double)dash->current_representation_->segtpl_.duration) / dash->current_representation_->segtpl_.timescale)) + 1);

                  if (dash->current_representation_->flags_ & DASHTree::Representation::INITIALIZATION)
                  {
                    s.range_begin_ = 0ULL, s.range_end_ = 0;
                    dash->current_representation_->initialization_ = s;
                  }
                  s.range_end_ = dash->current_representation_->startNumber_;
                }
                else
                  s.range_end_ = dash->current_representation_->segments_.data.back().range_end_ + 1;
                s.range_begin_ = s.startPTS_ = t;
                s.startPTS_ -= (dash->base_time_)*dash->current_representation_->segtpl_.timescale;
                
                for (; r; --r)
                {
                  dash->current_representation_->segments_.data.push_back(s);
                  ++s.range_end_;
                  s.range_begin_ = (t += d);
                  s.startPTS_ += d;
                }
                dash->current_representation_->nextPts_ = s.startPTS_;
              }
              else //Failure
              {
                dash->currentNode_ &= ~MPDNODE_SEGMENTTIMELINE;
                dash->current_representation_->timescale_ = 0;
              }
            }
            else if (strcmp(el, "SegmentTimeline") == 0)
            {
              dash->current_representation_->flags_ |= DASHTree::Representation::TIMELINE;
              dash->currentNode_ |= MPDNODE_SEGMENTTIMELINE;
            }
          }
          else if (dash->currentNode_ & MPDNODE_CONTENTPROTECTION)
          {
            if (strcmp(el, "cenc:pssh") == 0)
              dash->currentNode_ |= MPDNODE_PSSH;
            else if (strcmp(el, "widevine:license") == 0)
            {
              for (; *attr;)
              {
                if (strcmp((const char*)*attr, "robustness_level") == 0)
                  dash->need_secure_decoder_ = strncmp((const char*)*(attr + 1), "HW", 2) == 0;
                attr += 2;
              }
            }
          }
          else if (strcmp(el, "AudioChannelConfiguration") == 0)
          {
            dash->current_representation_->channelCount_ = GetChannels(attr);
          }
          else if (strcmp(el, "BaseURL") == 0) //Inside Representation
          {
            dash->strXMLText_.clear();
            dash->currentNode_ |= MPDNODE_BASEURL;
          }
          else if (strcmp(el, "SegmentList") == 0)
          {
            uint32_t dur(0), ts(0);
            for (; *attr;)
            {
              if (strcmp((const char*)*attr, "duration") == 0)
                dur = atoi((const char*)*(attr + 1));
              else if (strcmp((const char*)*attr, "timescale") == 0)
                ts = atoi((const char*)*(attr + 1));
              attr += 2;
            }
            if (ts && dur)
            {
              dash->current_representation_->duration_ = dur;
              dash->current_representation_->timescale_ = ts;
              dash->current_representation_->segments_.data.reserve(dash->estimate_segcount(
                dash->current_representation_->duration_,
                dash->current_representation_->timescale_));
            }
            else if (dash->current_adaptationset_->segment_durations_.data.size())
            {
              dash->current_representation_->segments_.data.reserve(
                dash->current_adaptationset_->segment_durations_.data.size());
            }
            else
              return;
            dash->currentNode_ |= MPDNODE_SEGMENTLIST;
          }
          else if (strcmp(el, "SegmentBase") == 0)
          {
            //<SegmentBase indexRangeExact = "true" indexRange = "867-1618">
            for (; *attr;)
            {
              if (strcmp((const char*)*attr, "indexRange") == 0)
                sscanf((const char*)*(attr + 1), "%u-%u" , &dash->current_representation_->indexRangeMin_, &dash->current_representation_->indexRangeMax_);
              else if (strcmp((const char*)*attr, "indexRangeExact") == 0 && strcmp((const char*)*(attr + 1), "true") == 0)
                dash->current_representation_->flags_ |= DASHTree::Representation::INDEXRANGEEXACT;
              dash->current_representation_->flags_ |= DASHTree::Representation::SEGMENTBASE;
              attr += 2;
            }
            if(dash->current_representation_->indexRangeMax_)
              dash->currentNode_ |= MPDNODE_SEGMENTLIST;
          }
          else if (strcmp(el, "SegmentTemplate") == 0)
          {
            dash->current_representation_->segtpl_ = dash->current_adaptationset_->segtpl_;

            dash->current_representation_->startNumber_ = ParseSegmentTemplate(attr, dash->current_representation_->url_, dash->current_representation_->segtpl_);
            ReplacePlaceHolders(dash->current_representation_->segtpl_.media, dash->current_representation_->id, dash->current_representation_->bandwidth_);
            dash->current_representation_->flags_ |= DASHTree::Representation::TEMPLATE;
            if (!dash->current_representation_->segtpl_.initialization.empty())
            {
              ReplacePlaceHolders(dash->current_representation_->segtpl_.initialization, dash->current_representation_->id, dash->current_representation_->bandwidth_);
              dash->current_representation_->flags_ |= DASHTree::Representation::INITIALIZATION;
              dash->current_representation_->url_ += dash->current_representation_->segtpl_.initialization;
              dash->current_representation_->timescale_ = dash->current_representation_->segtpl_.timescale;
            }
            dash->currentNode_ |= MPDNODE_SEGMENTTEMPLATE;
          }
          else if (strcmp(el, "ContentProtection") == 0)
          {
            if (!dash->current_representation_->pssh_set_ || dash->current_representation_->pssh_set_ == 0xFF)
            {
              //Mark protected but invalid
              dash->current_representation_->pssh_set_ = 0xFF;
              if (ParseContentProtection(attr, dash))
                dash->current_hasRepURN_ = true;
            }
          }
        }
        else if (dash->currentNode_ & (MPDNODE_SEGMENTTEMPLATE | MPDNODE_SEGMENTLIST))
        {
          if (dash->currentNode_ & MPDNODE_SEGMENTTIMELINE)
          {
            // <S t="3600" d="900000" r="2398"/>
            unsigned int d(0), r(1);
            uint64_t t(0);
            for (; *attr;)
            {
              if (strcmp((const char*)*attr, "t") == 0)
                t = atoll((const char*)*(attr + 1));
              else if (strcmp((const char*)*attr, "d") == 0)
                d = atoi((const char*)*(attr + 1));
              if (strcmp((const char*)*attr, "r") == 0)
                r = atoi((const char*)*(attr + 1))+1;
              attr += 2;
            }
            if(dash->current_adaptationset_->segment_durations_.data.empty())
              dash->current_adaptationset_->startPTS_ = dash->pts_helper_ = t;
            else if (t)
            {
              //Go back to the previous timestamp to calculate the real gap.
              dash->pts_helper_ -= dash->current_adaptationset_->segment_durations_.data.back();
              dash->current_adaptationset_->segment_durations_.data.back() = static_cast<uint32_t>(t - dash->pts_helper_);
              dash->pts_helper_ = t;
            }
            if (d && r)
            {
              for (; r; --r)
              {
                dash->current_adaptationset_->segment_durations_.data.push_back(d);
                dash->pts_helper_ += d;
              }
            }
          }
          else if (strcmp(el, "SegmentTimeline") == 0)
          {
            dash->currentNode_ |= MPDNODE_SEGMENTTIMELINE;
            dash->adp_timelined_ = true;
          }
        }
        else if (dash->currentNode_ & MPDNODE_SEGMENTDURATIONS)
        {
          if (strcmp(el, "S") == 0 && *(const char*)*attr == 'd')
            dash->current_adaptationset_->segment_durations_.data.push_back(atoi((const char*)*(attr + 1)));
        }
        else if (dash->currentNode_ & MPDNODE_CONTENTPROTECTION)
        {
          if (strcmp(el, "cenc:pssh") == 0)
            dash->currentNode_ |= MPDNODE_PSSH;
          else if (strcmp(el, "widevine:license") == 0)
          {
            for (; *attr;)
            {
              if (strcmp((const char*)*attr, "robustness_level") == 0)
                dash->need_secure_decoder_ = strncmp((const char*)*(attr + 1), "HW", 2) == 0;
              attr += 2;
            }
          }
        }
        else if (strcmp(el, "ContentComponent") == 0)
        {
          for (; *attr;)
          {
            if (strcmp((const char*)*attr, "contentType") == 0)
            {
              dash->current_adaptationset_->type_ =
                stricmp((const char*)*(attr + 1), "video") == 0 ? DASHTree::VIDEO
                : stricmp((const char*)*(attr + 1), "audio") == 0 ? DASHTree::AUDIO
                : stricmp((const char*)*(attr + 1), "text") == 0 ? DASHTree::SUBTITLE
                : DASHTree::NOTYPE;
              break;
            }
            attr += 2;
          }
        }
        else if (dash->currentNode_ & MPDNODE_BASEURL)
        {
        }
        else if (strcmp(el, "SegmentTemplate") == 0)
        {
          dash->current_adaptationset_->startNumber_ = ParseSegmentTemplate(attr, dash->current_adaptationset_->base_url_, dash->current_adaptationset_->segtpl_);
          dash->current_adaptationset_->timescale_ = dash->current_adaptationset_->segtpl_.timescale;
          dash->currentNode_ |= MPDNODE_SEGMENTTEMPLATE;
        }
        else if (strcmp(el, "SegmentList") == 0)
        {
          for (; *attr;)
          {
            if (strcmp((const char*)*attr, "duration") == 0)
              dash->current_adaptationset_->duration_ = atoi((const char*)*(attr + 1));
            else if (strcmp((const char*)*attr, "timescale") == 0)
              dash->current_adaptationset_->timescale_ = atoi((const char*)*(attr + 1));
            attr += 2;
          }
          dash->currentNode_ |= MPDNODE_SEGMENTLIST;
        }
        else if (strcmp(el, "Representation") == 0)
        {
          dash->current_representation_ = new DASHTree::Representation();
          dash->current_representation_->channelCount_ = dash->adpChannelCount_;
          dash->current_representation_->codecs_ = dash->current_adaptationset_->codecs_;
          dash->current_representation_->url_ = dash->current_adaptationset_->base_url_;
          dash->current_representation_->timescale_ = dash->current_adaptationset_->timescale_;
          dash->current_representation_->duration_ = dash->current_adaptationset_->duration_;
          dash->current_representation_->startNumber_ = dash->current_adaptationset_->startNumber_;
          dash->current_adaptationset_->repesentations_.push_back(dash->current_representation_);
          dash->current_representation_->width_ = dash->adpwidth_;
          dash->current_representation_->height_ = dash->adpheight_;
          dash->current_representation_->fpsRate_ = dash->adpfpsRate_;
          dash->current_representation_->aspect_ = dash->adpaspect_;
          dash->current_representation_->containerType_ = dash->adpContainerType_;

          dash->current_pssh_.clear();
          dash->current_hasRepURN_ = false;

          for (; *attr;)
          {
            if (strcmp((const char*)*attr, "bandwidth") == 0)
              dash->current_representation_->bandwidth_ = atoi((const char*)*(attr + 1));
            else if (strcmp((const char*)*attr, "codecs") == 0)
              dash->current_representation_->codecs_ = (const char*)*(attr + 1);
            else if (strcmp((const char*)*attr, "width") == 0)
              dash->current_representation_->width_ = static_cast<uint16_t>(atoi((const char*)*(attr + 1)));
            else if (strcmp((const char*)*attr, "height") == 0)
              dash->current_representation_->height_ = static_cast<uint16_t>(atoi((const char*)*(attr + 1)));
            else if (strcmp((const char*)*attr, "audioSamplingRate") == 0)
              dash->current_representation_->samplingRate_ = static_cast<uint32_t>(atoi((const char*)*(attr + 1)));
            else if (strcmp((const char*)*attr, "frameRate") == 0)
              sscanf((const char*)*(attr + 1), "%" SCNu32 "/%" SCNu32, &dash->current_representation_->fpsRate_, &dash->current_representation_->fpsScale_);
            else if (strcmp((const char*)*attr, "id") == 0)
              dash->current_representation_->id = (const char*)*(attr + 1);
            else if (strcmp((const char*)*attr, "codecPrivateData") == 0)
              dash->current_representation_->codec_private_data_ = annexb_to_avc((const char*)*(attr + 1));
            else if (strcmp((const char*)*attr, "hdcp") == 0)
              dash->current_representation_->hdcpVersion_ = static_cast<uint16_t>(atof((const char*)*(attr + 1))*10);
            else if (dash->current_adaptationset_->mimeType_.empty() && strcmp((const char*)*attr, "mimeType") == 0)
            {
              dash->current_adaptationset_->mimeType_ = (const char*)*(attr + 1);
              if (dash->current_adaptationset_->type_ == DASHTree::NOTYPE)
              {
                if (strncmp(dash->current_adaptationset_->mimeType_.c_str(), "video", 5) == 0)
                  dash->current_adaptationset_->type_ = DASHTree::VIDEO;
                else if (strncmp(dash->current_adaptationset_->mimeType_.c_str(), "audio", 5) == 0)
                  dash->current_adaptationset_->type_ = DASHTree::AUDIO;
                else if (strncmp(dash->current_adaptationset_->mimeType_.c_str(), "application", 11) == 0)
                  dash->current_adaptationset_->type_ = DASHTree::SUBTITLE;
              }
              if (strstr(dash->current_adaptationset_->mimeType_.c_str(), "/webm"))
                dash->current_representation_->containerType_ = AdaptiveTree::CONTAINERTYPE_WEBM;
              else if (strstr(dash->current_adaptationset_->mimeType_.c_str(), "/x-matroska"))
                dash->current_representation_->containerType_ = AdaptiveTree::CONTAINERTYPE_MATROSKA;
            }
            attr += 2;
          }

          if (dash->current_adaptationset_->type_ == DASHTree::SUBTITLE
          && dash->current_adaptationset_->mimeType_ == "application/ttml+xml")
            dash->current_representation_->flags_ |= DASHTree::Representation::SUBTITLESTREAM;

          dash->currentNode_ |= MPDNODE_REPRESENTATION;
        }
        else if (strcmp(el, "SegmentDurations") == 0)
        {
          dash->current_adaptationset_->segment_durations_.data.reserve(dash->segcount_);
          for (; *attr;)
          {
            if (strcmp((const char*)*attr, "timescale") == 0)
            {
              dash->current_adaptationset_->timescale_ = atoi((const char*)*(attr + 1));
              break;
            }
            attr += 2;
          }
          dash->currentNode_ |= MPDNODE_SEGMENTDURATIONS;
        }
        else if (strcmp(el, "ContentProtection") == 0)
        {
          if (!dash->adp_pssh_set_ || dash->adp_pssh_set_== 0xFF)
          {
            //Mark protected but invalid
            dash->adp_pssh_set_ = 0xFF;
            if (ParseContentProtection(attr, dash))
              dash->current_hasAdpURN_ = true;
          }
        }
        else if (strcmp(el, "AudioChannelConfiguration") == 0)
        {
          dash->adpChannelCount_ = GetChannels(attr);
        }
        else if (strcmp(el, "BaseURL") == 0) //Inside AdaptationSet
        {
          dash->strXMLText_.clear();
          dash->currentNode_ |= MPDNODE_BASEURL;
        }
      }
      else if (dash->currentNode_ & (MPDNODE_SEGMENTLIST | MPDNODE_SEGMENTTEMPLATE))
      {
        if (dash->currentNode_ & MPDNODE_SEGMENTTIMELINE)
        {
          // <S t="3600" d="900000" r="2398"/>
          unsigned int d(0), r(1);
          uint64_t t(0);
          for (; *attr;)
          {
            if (strcmp((const char*)*attr, "t") == 0)
              t = atoll((const char*)*(attr + 1));
            else if (strcmp((const char*)*attr, "d") == 0)
              d = atoi((const char*)*(attr + 1));
            if (strcmp((const char*)*attr, "r") == 0)
              r = atoi((const char*)*(attr + 1)) + 1;
            attr += 2;
          }
          if (dash->current_period_->segment_durations_.data.empty())
          {
            if (!dash->current_period_->duration_ && d)
              dash->current_period_->segment_durations_.data.reserve(dash->estimate_segcount(d, dash->current_period_->timescale_));
            dash->current_period_->startPTS_ = dash->pts_helper_ = t;
          }
          else if (t)
          {
            //Go back to the previous timestamp to calculate the real gap.
            dash->pts_helper_ -= dash->current_adaptationset_->segment_durations_.data.back();
            dash->current_period_->segment_durations_.data.back() = static_cast<uint32_t>(t - dash->pts_helper_);
            dash->pts_helper_ = t;
          }
          if (d && r)
          {
            for (; r; --r)
            {
              dash->current_period_->segment_durations_.data.push_back(d);
              dash->pts_helper_ += d;
            }
          }
        }
        else if (strcmp(el, "SegmentTimeline") == 0)
        {
          dash->currentNode_ |= MPDNODE_SEGMENTTIMELINE;
          dash->period_timelined_ = true;
        }
      }
      else if (strcmp(el, "AdaptationSet") == 0)
      {
        //<AdaptationSet contentType="video" group="2" lang="en" mimeType="video/mp4" par="16:9" segmentAlignment="true" startWithSAP="1" subsegmentAlignment="true" subsegmentStartsWithSAP="1">
        dash->current_adaptationset_ = new DASHTree::AdaptationSet();
        dash->current_period_->adaptationSets_.push_back(dash->current_adaptationset_);
        dash->current_adaptationset_->base_url_ = dash->current_period_->base_url_;
        dash->current_pssh_.clear();
        dash->adpChannelCount_ = 0;
        dash->adpwidth_ = 0;
        dash->adpheight_ = 0;
        dash->adpfpsRate_ = 0;
        dash->adpaspect_ = 0.0f;
        dash->adp_pssh_set_ = 0;
        dash->adpContainerType_ = AdaptiveTree::CONTAINERTYPE_MP4;
        dash->current_hasAdpURN_ = false;
        dash->adp_timelined_ = dash->period_timelined_;
        dash->current_adaptationset_->timescale_ = dash->current_period_->timescale_;
        dash->current_adaptationset_->duration_ = dash->current_period_->duration_;
        dash->current_adaptationset_->segment_durations_ = dash->current_period_->segment_durations_;
        dash->current_adaptationset_->segtpl_ = dash->current_period_->segtpl_;
        dash->current_adaptationset_->startNumber_ = dash->current_period_->startNumber_;

        for (; *attr;)
        {
          if (strcmp((const char*)*attr, "contentType") == 0)
            dash->current_adaptationset_->type_ =
            stricmp((const char*)*(attr + 1), "video") == 0 ? DASHTree::VIDEO
            : stricmp((const char*)*(attr + 1), "audio") == 0 ? DASHTree::AUDIO
            : stricmp((const char*)*(attr + 1), "text") == 0 ? DASHTree::SUBTITLE
            : DASHTree::NOTYPE;
          else if (strcmp((const char*)*attr, "id") == 0)
            dash->current_adaptationset_->id_ = (const char*)*(attr + 1);
          else if (strcmp((const char*)*attr, "group") == 0)
            dash->current_adaptationset_->group_ = (const char*)*(attr + 1);
          else if (strcmp((const char*)*attr, "lang") == 0)
            dash->current_adaptationset_->language_ = ltranslate((const char*)*(attr + 1));
          else if (strcmp((const char*)*attr, "mimeType") == 0)
            dash->current_adaptationset_->mimeType_ = (const char*)*(attr + 1);
          else if (strcmp((const char*)*attr, "codecs") == 0)
            dash->current_adaptationset_->codecs_ = (const char*)*(attr + 1);
          else if (strcmp((const char*)*attr, "width") == 0)
            dash->adpwidth_ = static_cast<uint16_t>(atoi((const char*)*(attr + 1)));
          else if (strcmp((const char*)*attr, "height") == 0)
            dash->adpheight_ = static_cast<uint16_t>(atoi((const char*)*(attr + 1)));
          else if (strcmp((const char*)*attr, "frameRate") == 0)
            dash->adpfpsRate_ = static_cast<uint32_t>(atoi((const char*)*(attr + 1)));
          else if (strcmp((const char*)*attr, "par") == 0)
          {
            int w, h;
            if (sscanf((const char*)*(attr + 1), "%d:%d", &w, &h) == 2)
              dash->adpaspect_ = (float)w / h;
          }
          else if (strcmp((const char*)*attr, "impaired") == 0)
          {
            dash->current_adaptationset_->impaired_ = strcmp((const char*)*(attr + 1), "true") == 0;
          }
          attr += 2;
        }
        if (dash->current_adaptationset_->type_ == DASHTree::NOTYPE)
        {
          if (strncmp(dash->current_adaptationset_->mimeType_.c_str(), "video", 5) == 0)
            dash->current_adaptationset_->type_ = DASHTree::VIDEO;
          else if (strncmp(dash->current_adaptationset_->mimeType_.c_str(), "audio", 5) == 0)
            dash->current_adaptationset_->type_ = DASHTree::AUDIO;
          else if (strncmp(dash->current_adaptationset_->mimeType_.c_str(), "application", 11) == 0)
            dash->current_adaptationset_->type_ = DASHTree::SUBTITLE;
        }

        if (strstr(dash->current_adaptationset_->mimeType_.c_str(), "/webm"))
          dash->adpContainerType_ = AdaptiveTree::CONTAINERTYPE_WEBM;
        else if (strstr(dash->current_adaptationset_->mimeType_.c_str(), "/x-matroska"))
          dash->adpContainerType_ = AdaptiveTree::CONTAINERTYPE_MATROSKA;

        dash->segcount_ = 0;
        dash->currentNode_ |= MPDNODE_ADAPTIONSET;
      }
      else if (strcmp(el, "SegmentTemplate") == 0)
      {
        dash->current_period_->startNumber_ = ParseSegmentTemplate(attr, dash->current_period_->base_url_, dash->current_period_->segtpl_);
        dash->current_period_->timescale_ = dash->current_period_->segtpl_.timescale;
        dash->currentNode_ |= MPDNODE_SEGMENTTEMPLATE;
      }
      else if (strcmp(el, "SegmentList") == 0)
      {
        for (; *attr;)
        {
          if (strcmp((const char*)*attr, "duration") == 0)
            dash->current_period_->duration_ = atoi((const char*)*(attr + 1));
          else if (strcmp((const char*)*attr, "timescale") == 0)
            dash->current_period_->timescale_ = atoi((const char*)*(attr + 1));
          else if (strcmp((const char*)*attr, "startNumber") == 0)
            dash->current_period_->startNumber_ = atoi((const char*)*(attr + 1));
          attr += 2;
        }
        if (dash->current_period_->timescale_)
        {
          if (dash->current_period_->duration_)
            dash->current_period_->segment_durations_.data.reserve(dash->estimate_segcount(
              dash->current_period_->duration_,
              dash->current_period_->timescale_));
          dash->currentNode_ |= MPDNODE_SEGMENTLIST;
        }
      }
      else if (strcmp(el, "BaseURL") == 0) // Inside Period
      {
        dash->strXMLText_.clear();
        dash->currentNode_ |= MPDNODE_BASEURL;
      }
    }
    else if (strcmp(el, "BaseURL") == 0) // Out of Period
    {
      dash->strXMLText_.clear();
      dash->currentNode_ |= MPDNODE_BASEURL;
    }
    else if (strcmp(el, "Period") == 0)
    {
      dash->current_period_ = new DASHTree::Period();
      dash->current_period_->base_url_ = dash->base_url_;
      dash->periods_.push_back(dash->current_period_);
      dash->period_timelined_ = false;
      dash->currentNode_ |= MPDNODE_PERIOD;
    }
  }
  else if (strcmp(el, "MPD") == 0)
  {
    const char *mpt(0), *tsbd(0);
    bool bStatic(false);

    dash->firstStartNumber_ = 0;

    dash->overallSeconds_ = 0;
    dash->stream_start_ = time(0);

    for (; *attr;)
    {
      if (strcmp((const char*)*attr, "mediaPresentationDuration") == 0)
        mpt = (const char*)*(attr + 1);
      else if (strcmp((const char*)*attr, "type") == 0)
      {
        bStatic = strcmp((const char*)*(attr + 1), "static") == 0;
      }
      else if (strcmp((const char*)*attr, "timeShiftBufferDepth") == 0)
      {
        tsbd = (const char*)*(attr + 1);
        dash->has_timeshift_buffer_ = true;
      }
      else if (strcmp((const char*)*attr, "availabilityStartTime") == 0)
      {
        dash->available_time_ = getTime((const char*)*(attr + 1));
        if (!dash->available_time_)
          dash->available_time_ = ~0ULL;
      }
      else if (strcmp((const char*)*attr, "publishTime") == 0)
        dash->publish_time_ = getTime((const char*)*(attr + 1));
      else if (strcmp((const char*)*attr, "minimumUpdatePeriod") == 0)
      {
        uint64_t dur(0);
        AddDuration((const char*)*(attr + 1), dur, 1500);
        dash->SetUpdateInterval(static_cast<uint32_t>(dur));
      }
      attr += 2;
    }

    if (!~dash->available_time_)
      dash->available_time_ = dash->publish_time_;

    if (!mpt)
      mpt = tsbd;
    else if (bStatic)
      dash->has_timeshift_buffer_ = false;

    AddDuration(mpt, dash->overallSeconds_, 1);

    if (dash->publish_time_ && dash->available_time_ && dash->publish_time_ - dash->available_time_ > dash->overallSeconds_)
      dash->base_time_ = dash->publish_time_ - dash->available_time_ - dash->overallSeconds_;
    dash->minPresentationOffset = ~0ULL;

    dash->currentNode_ |= MPDNODE_MPD;
  }
}

/*----------------------------------------------------------------------
|   expat text
+---------------------------------------------------------------------*/
static void XMLCALL
text(void *data, const char *s, int len)
{
  DASHTree *dash(reinterpret_cast<DASHTree*>(data));
  if (dash->currentNode_ & (MPDNODE_BASEURL | MPDNODE_PSSH))
    dash->strXMLText_ += std::string(s, len);
}

/*----------------------------------------------------------------------
|   expat end
+---------------------------------------------------------------------*/

static void XMLCALL
end(void *data, const char *el)
{
  DASHTree *dash(reinterpret_cast<DASHTree*>(data));

  if (dash->currentNode_ & MPDNODE_MPD)
  {
    if (dash->currentNode_ & MPDNODE_PERIOD)
    {
      if (dash->currentNode_ & MPDNODE_ADAPTIONSET)
      {
        if (dash->currentNode_ & MPDNODE_REPRESENTATION)
        {
          if (dash->currentNode_ & MPDNODE_BASEURL) // Inside Representation
          {
            if (strcmp(el, "BaseURL") == 0)
            {
              while (dash->strXMLText_.size() && (dash->strXMLText_[0] == '\n' || dash->strXMLText_[0] == '\r'))
                dash->strXMLText_.erase(dash->strXMLText_.begin());

              if (dash->strXMLText_.compare(0, 7, "http://") == 0
                || dash->strXMLText_.compare(0, 8, "https://") == 0)
                dash->current_representation_->url_ = dash->strXMLText_;
              else
                dash->current_representation_->url_ += dash->strXMLText_;
              dash->currentNode_ &= ~MPDNODE_BASEURL;
            }
          }
          else if (dash->currentNode_ & MPDNODE_SEGMENTLIST)
          {
            if (strcmp(el, "SegmentList") == 0 || strcmp(el, "SegmentBase") == 0)
            {
              dash->currentNode_ &= ~MPDNODE_SEGMENTLIST;
              if (!dash->segcount_)
                dash->segcount_ = dash->current_representation_->segments_.data.size();
            }
          }
          else if (dash->currentNode_ & MPDNODE_SEGMENTTEMPLATE)
          {
            if (dash->currentNode_ & MPDNODE_SEGMENTTIMELINE)
            {
              if (strcmp(el, "SegmentTimeline") == 0)
                dash->currentNode_ &= ~MPDNODE_SEGMENTTIMELINE;
            }
            else if (strcmp(el, "SegmentTemplate") == 0)
            {
              dash->currentNode_ &= ~MPDNODE_SEGMENTTEMPLATE;
            }
          }
          else if (dash->currentNode_ & MPDNODE_CONTENTPROTECTION)
          {
            if (dash->currentNode_ & MPDNODE_PSSH)
            {
              if (strcmp(el, "cenc:pssh") == 0)
              {
                dash->current_pssh_ = dash->strXMLText_;
                dash->currentNode_ &= ~MPDNODE_PSSH;
              }
            }
            else if (strcmp(el, "ContentProtection") == 0)
            {
              if (dash->current_pssh_.empty())
                dash->current_pssh_ = "FILE";
              dash->current_representation_->pssh_set_ = static_cast<uint8_t>(dash->insert_psshset(dash->current_adaptationset_->type_));
              dash->currentNode_ &= ~MPDNODE_CONTENTPROTECTION;
            }
          }
          else if (strcmp(el, "Representation") == 0)
          {
            dash->currentNode_ &= ~MPDNODE_REPRESENTATION;

            if (dash->current_representation_->pssh_set_ == 0xFF)
            {
              // Some manifests dont have Protection per URN included.
              // We treet manifests valid if no single URN was given
              if (!dash->current_hasRepURN_)
              {
                dash->current_pssh_ = "FILE";
                dash->current_representation_->pssh_set_ = static_cast<uint8_t>(dash->insert_psshset(dash->current_adaptationset_->type_));
                dash->encryptionState_ |= DASHTree::ENCRYTIONSTATE_SUPPORTED;
              }
              else
              {
                delete dash->current_representation_;
                dash->current_adaptationset_->repesentations_.pop_back();
                return;
              }
            }

            if (dash->current_representation_->segments_.data.empty())
            {
              bool isSegmentTpl(!dash->current_representation_->segtpl_.media.empty());
              DASHTree::SegmentTemplate &tpl(isSegmentTpl ? dash->current_representation_->segtpl_ : dash->current_adaptationset_->segtpl_);

              if (!tpl.media.empty() && dash->overallSeconds_ > 0
                && tpl.timescale > 0 && (tpl.duration > 0 || dash->current_adaptationset_->segment_durations_.data.size()))
              {
                unsigned int countSegs = !dash->current_adaptationset_->segment_durations_.data.empty()? dash->current_adaptationset_->segment_durations_.data.size():
                  (unsigned int)((double)dash->overallSeconds_ / (((double)tpl.duration) / tpl.timescale)) + 1;

                if (countSegs < 65536)
                {
                  DASHTree::Segment seg;

                  dash->current_representation_->flags_ |= DASHTree::Representation::TEMPLATE;

                  dash->current_representation_->segments_.data.reserve(countSegs);
                  if (!tpl.initialization.empty())
                  {
                    seg.range_end_ = ~0;
                    if (!isSegmentTpl)
                    {
                      dash->current_representation_->url_ += tpl.initialization;
                      ReplacePlaceHolders(dash->current_representation_->url_, dash->current_representation_->id, dash->current_representation_->bandwidth_);
                      dash->current_representation_->segtpl_.media = tpl.media;
                      ReplacePlaceHolders(dash->current_representation_->segtpl_.media, dash->current_representation_->id, dash->current_representation_->bandwidth_);
                    }

                    dash->current_representation_->initialization_ = seg;
                    dash->current_representation_->flags_ |= DASHTree::Representation::INITIALIZATION;
                  }

                  std::vector<uint32_t>::const_iterator sdb(dash->current_adaptationset_->segment_durations_.data.begin()),
                    sde(dash->current_adaptationset_->segment_durations_.data.end());
                  bool timeBased = sdb!=sde && tpl.media.find("$Time") != std::string::npos;
                  if (dash->adp_timelined_)
                    dash->current_representation_->flags_ |= AdaptiveTree::Representation::TIMELINE;

                  seg.range_end_ = isSegmentTpl ? dash->current_representation_->startNumber_: dash->current_adaptationset_->startNumber_;
                  seg.startPTS_ = dash->current_adaptationset_->startPTS_ - (dash->base_time_)*dash->current_adaptationset_->timescale_;
                  seg.range_begin_ = dash->current_adaptationset_->startPTS_;

                  if (!timeBased && dash->available_time_ && dash->stream_start_ - dash->available_time_ > dash->overallSeconds_) //we need to adjust the start-segment
                    seg.range_end_ += static_cast<uint64_t>(((dash->stream_start_ - dash->available_time_ - dash->overallSeconds_)*tpl.timescale) / tpl.duration);

                  for (;countSegs;--countSegs)
                  {
                    dash->current_representation_->segments_.data.push_back(seg);
                    uint32_t duration((sdb != sde) ? *(sdb++) : tpl.duration);
                    seg.startPTS_ += duration, seg.range_begin_ += duration;
                    ++seg.range_end_;
                  }
                  dash->current_representation_->nextPts_ = seg.startPTS_;
                  return;
                }
              }
              else if (!(dash->current_representation_->flags_ & (DASHTree::Representation::SEGMENTBASE | DASHTree::Representation::SUBTITLESTREAM)))
              {
                //Let us try to extract the fragments out of SIDX atom  
                dash->current_representation_->flags_ |= DASHTree::Representation::SEGMENTBASE;
                dash->current_representation_->indexRangeMin_ = 0;
                dash->current_representation_->indexRangeMax_ = 1024 * 200;
              }
            }
            else
            {
              ReplacePlaceHolders(dash->current_representation_->url_, dash->current_representation_->id, dash->current_representation_->bandwidth_);
              ReplacePlaceHolders(dash->current_representation_->segtpl_.media, dash->current_representation_->id, dash->current_representation_->bandwidth_);
            }

            if ((dash->current_representation_->flags_ & AdaptiveTree::Representation::INITIALIZATION) == 0 && !dash->current_representation_->segments_.empty())
              // we assume that we have a MOOV atom included in each segment (max 100k = youtube)
              dash->current_representation_->flags_ |= AdaptiveTree::Representation::INITIALIZATION_PREFIXED;

            if (dash->current_representation_->startNumber_ > dash->firstStartNumber_)
              dash->firstStartNumber_ = dash->current_representation_->startNumber_;
          }
        }
        else if (dash->currentNode_ & MPDNODE_SEGMENTDURATIONS)
        {
          if (strcmp(el, "SegmentDurations") == 0)
            dash->currentNode_ &= ~MPDNODE_SEGMENTDURATIONS;
        }
        else if (dash->currentNode_ & MPDNODE_BASEURL) // Inside AdaptationSet
        {
          if (strcmp(el, "BaseURL") == 0)
          {
            dash->current_adaptationset_->base_url_ = dash->current_period_->base_url_ + dash->strXMLText_;
            dash->currentNode_ &= ~MPDNODE_BASEURL;
          }
        }
        else if (dash->currentNode_ & (MPDNODE_SEGMENTLIST | MPDNODE_SEGMENTTEMPLATE))
        {
          if (dash->currentNode_ & MPDNODE_SEGMENTTIMELINE)
          {
            if (strcmp(el, "SegmentTimeline") == 0)
              dash->currentNode_ &= ~MPDNODE_SEGMENTTIMELINE;
          }
          else if (strcmp(el, "SegmentTemplate") == 0)
          {
            dash->currentNode_ &= ~MPDNODE_SEGMENTTEMPLATE;
          }
          else if (strcmp(el, "SegmentList") == 0)
          {
            dash->currentNode_ &= ~MPDNODE_SEGMENTLIST;
          }
        }
        else if (dash->currentNode_ & MPDNODE_CONTENTPROTECTION)
        {
          if (dash->currentNode_ & MPDNODE_PSSH)
          {
            if (strcmp(el, "cenc:pssh") == 0)
            {
              dash->current_pssh_ = dash->strXMLText_;
              dash->currentNode_ &= ~MPDNODE_PSSH;
            }
          }
          else if (strcmp(el, "ContentProtection") == 0)
          {
            if (dash->current_pssh_.empty())
              dash->current_pssh_ = "FILE";
            dash->adp_pssh_set_ = static_cast<uint8_t>(dash->insert_psshset(dash->current_adaptationset_->type_));
            dash->currentNode_ &= ~MPDNODE_CONTENTPROTECTION;
          }
        }
        else if (strcmp(el, "AdaptationSet") == 0)
        {
          dash->currentNode_ &= ~MPDNODE_ADAPTIONSET;
          if (dash->current_adaptationset_->type_ == DASHTree::NOTYPE
          || (dash->adp_pssh_set_ == 0xFF && dash->current_hasRepURN_)
          || dash->current_adaptationset_->repesentations_.empty())
          {
            delete dash->current_adaptationset_;
            dash->current_period_->adaptationSets_.pop_back();
          }
          else
          {
            if (dash->adp_pssh_set_)
            {
              if (dash->adp_pssh_set_ == 0xFF)
              {
                dash->current_pssh_ = "FILE";
                dash->adp_pssh_set_ = static_cast<uint8_t>(dash->insert_psshset(dash->current_adaptationset_->type_));
                dash->encryptionState_ |= DASHTree::ENCRYTIONSTATE_SUPPORTED;
              }

              for (std::vector<DASHTree::Representation*>::iterator
                b(dash->current_adaptationset_->repesentations_.begin()),
                e(dash->current_adaptationset_->repesentations_.end()); b != e; ++b)
                if (!(*b)->pssh_set_)
                  (*b)->pssh_set_ = dash->adp_pssh_set_;
            }

            if (dash->current_adaptationset_->segment_durations_.data.empty()
              && !dash->current_adaptationset_->segtpl_.media.empty())
            {
              for (std::vector<DASHTree::Representation*>::iterator 
                b(dash->current_adaptationset_->repesentations_.begin()), 
                e(dash->current_adaptationset_->repesentations_.end()); b != e; ++b)
              {
                if (!(*b)->duration_ || !(*b)->timescale_)
                {
                  (*b)->duration_ = dash->current_adaptationset_->segtpl_.duration;
                  (*b)->timescale_ = dash->current_adaptationset_->segtpl_.timescale;
                }
              }
            }
            else if (!dash->current_adaptationset_->segment_durations_.data.empty())
            //If representation are not timelined, we have to adjust startPTS_ in rep::segments
            {
              for (std::vector<DASHTree::Representation*>::iterator
                b(dash->current_adaptationset_->repesentations_.begin()),
                e(dash->current_adaptationset_->repesentations_.end()); b != e; ++b)
              {
                if ((*b)->flags_ & DASHTree::Representation::TIMELINE)
                  continue;
                std::vector<uint32_t>::const_iterator sdb(dash->current_adaptationset_->segment_durations_.data.begin()),
                  sde(dash->current_adaptationset_->segment_durations_.data.end());
                uint64_t spts(0);
                for (std::vector<DASHTree::Segment>::iterator sb((*b)->segments_.data.begin()), se((*b)->segments_.data.end()); sb != se && sdb != sde; ++sb, ++sdb)
                {
                  sb->startPTS_ = spts;
                  spts += *sdb;
                }
                (*b)->nextPts_ = spts;
              }
            }
          }
        }
      }
      else if (dash->currentNode_ & MPDNODE_BASEURL) // Inside Period
      {
        if (strcmp(el, "BaseURL") == 0)
        {
          while (dash->strXMLText_.size() && (dash->strXMLText_[0] == '\n' || dash->strXMLText_[0] == '\r'))
            dash->strXMLText_.erase(dash->strXMLText_.begin());
          if (dash->strXMLText_.compare(0, 7, "http://") == 0
            || dash->strXMLText_.compare(0, 8, "https://") == 0)
            dash->current_period_->base_url_ = dash->strXMLText_;
          else
            dash->current_period_->base_url_ += dash->strXMLText_;
          dash->currentNode_ &= ~MPDNODE_BASEURL;
        }
      }
      else if (dash->currentNode_ & (MPDNODE_SEGMENTLIST | MPDNODE_SEGMENTTEMPLATE))
      {
        if (dash->currentNode_ & MPDNODE_SEGMENTTIMELINE)
        {
          if (strcmp(el, "SegmentTimeline") == 0)
            dash->currentNode_ &= ~MPDNODE_SEGMENTTIMELINE;

        }
        else if (strcmp(el, "SegmentList") == 0)
          dash->currentNode_ &= ~MPDNODE_SEGMENTLIST;
        else if (strcmp(el, "SegmentTemplate") == 0)
          dash->currentNode_ &= ~MPDNODE_SEGMENTTEMPLATE;
      }
      else if (strcmp(el, "Period") == 0)
      {
        dash->currentNode_ &= ~MPDNODE_PERIOD;
      }
    }
    else if (dash->currentNode_ & MPDNODE_BASEURL) // Outside Period
    {
      if (strcmp(el, "BaseURL") == 0)
      {
        while (dash->strXMLText_.size() && (dash->strXMLText_[0] == '\n' || dash->strXMLText_[0] == '\r'))
          dash->strXMLText_.erase(dash->strXMLText_.begin());
        if (dash->strXMLText_.compare(0, 7, "http://") == 0
          || dash->strXMLText_.compare(0, 8, "https://") == 0)
          dash->base_url_ = dash->strXMLText_;
        else
          dash->base_url_ += dash->strXMLText_;
        dash->currentNode_ &= ~MPDNODE_BASEURL;
      }
    }
    else if (strcmp(el, "MPD") == 0)
    {
      dash->currentNode_ &= ~MPDNODE_MPD;
    }
  }
}

/*----------------------------------------------------------------------
|   DASHTree
+---------------------------------------------------------------------*/

bool DASHTree::open(const std::string &url, const std::string &manifestUpdateParam)
{
  PreparePaths(url, manifestUpdateParam);
  parser_ = XML_ParserCreate(NULL);
  if (!parser_)
    return false;

  XML_SetUserData(parser_, (void*)this);
  XML_SetElementHandler(parser_, start, end);
  XML_SetCharacterDataHandler(parser_, text);
  currentNode_ = 0;
  strXMLText_.clear();

  bool ret = download(manifest_url_.c_str(), manifest_headers_);

  XML_ParserFree(parser_);
  parser_ = 0;

  if (ret)
  {
    SortTree();
    StartUpdateThread();
  }
  return ret;
}

bool DASHTree::write_data(void *buffer, size_t buffer_size, void *opaque)
{
  bool done(false);
  XML_Status retval = XML_Parse(parser_, (const char*)buffer, buffer_size, done);

  if (retval == XML_STATUS_ERROR)
  {
    //unsigned int byteNumber = XML_GetErrorByteIndex(parser_);
    return false;
  }
  return true;
}

//Called each time before we switch to a new segment
void DASHTree::RefreshSegments(Representation *rep, StreamType type)
{
  if ((type == VIDEO || type == AUDIO))
  {
    lastUpdated_ = std::chrono::system_clock::now();
    RefreshUpdateThread();
    RefreshSegments();
  }
}

//Can be called form update-thread!
void DASHTree::RefreshSegments()
{
  if (has_timeshift_buffer_ && !update_parameter_.empty())
  {
    std::string replaced;
    uint32_t numReplace = ~0U;
    unsigned int nextStartNumber(~0);

    if (~update_parameter_pos_)
    {
      for (std::vector<Period*>::const_iterator bp(periods_.begin()), ep(periods_.end()); bp != ep; ++bp)
        for (std::vector<AdaptationSet*>::const_iterator ba((*bp)->adaptationSets_.begin()), ea((*bp)->adaptationSets_.end()); ba != ea; ++ba)
          for (std::vector<Representation*>::iterator br((*ba)->repesentations_.begin()), er((*ba)->repesentations_.end()); br != er; ++br)
          {
            if ((*br)->startNumber_ + (*br)->segments_.size() < nextStartNumber)
              nextStartNumber = (*br)->startNumber_ + (*br)->segments_.size();
            uint32_t replaceable = (*br)->getCurrentSegmentPos() + 1;
            if (!replaceable)
              replaceable = (*br)->segments_.size();
            if (replaceable < numReplace)
              numReplace = replaceable;
          }
      Log(LOGLEVEL_DEBUG, "DASH Update: numReplace: %u", numReplace);
      replaced = update_parameter_;
      char buf[32];
      sprintf(buf, "%u", nextStartNumber);
      replaced.replace(update_parameter_pos_, 14, buf);
    }

    DASHTree updateTree;
    updateTree.manifest_headers_ = manifest_headers_;
    if (!~update_parameter_pos_)
    {
      if (!etag_.empty())
        updateTree.manifest_headers_["If-None-Match"] = "\"" + etag_ + "\"";
      if (!last_modified_.empty())
        updateTree.manifest_headers_["If-Modified-Since"] = last_modified_;
    }

    if (updateTree.open(manifest_url_ + replaced, ""))
    {
      etag_ = updateTree.etag_;
      last_modified_ = updateTree.last_modified_;

      //Youtube returns last smallest number in case the requested data is not available
      if (~update_parameter_pos_ && updateTree.firstStartNumber_ < nextStartNumber)
          return;

      std::vector<Period*>::const_iterator bpd(periods_.begin()), epd(periods_.end());
      for (std::vector<Period*>::const_iterator bp(updateTree.periods_.begin()), ep(updateTree.periods_.end()); bp != ep && bpd != epd; ++bp, ++bpd)
      {
        for (std::vector<AdaptationSet*>::const_iterator ba((*bp)->adaptationSets_.begin()), ea((*bp)->adaptationSets_.end()); ba != ea; ++ba)
        {
          //Locate adaptationset
          std::vector<AdaptationSet*>::const_iterator bad((*bpd)->adaptationSets_.begin()), ead((*bpd)->adaptationSets_.end());
          for (; bad != ead && ((*bad)->id_ != (*ba)->id_ || (*bad)->group_ != (*ba)->group_); ++bad);
          if (bad != ead)
          {
            for (std::vector<Representation*>::iterator br((*ba)->repesentations_.begin()), er((*ba)->repesentations_.end()); br != er; ++br)
            {
              //Locate representation
              std::vector<Representation*>::const_iterator brd((*bad)->repesentations_.begin()), erd((*bad)->repesentations_.end());
              for (; brd != erd && (*brd)->id != (*br)->id; ++brd);
              if (brd != erd && !(*br)->segments_.empty())
              {
                if (~update_parameter_pos_) // partitial update
                {
                  //Here we go -> Insert new segments
                  uint64_t ptsOffset = (*brd)->nextPts_ - (*br)->segments_[0]->startPTS_;
                  uint32_t currentPos = (*brd)->getCurrentSegmentPos();
                  unsigned int repFreeSegments(numReplace);
                  std::vector<Segment>::iterator bs((*br)->segments_.data.begin()), es((*br)->segments_.data.end());
                  for (; bs != es && repFreeSegments; ++bs)
                  {
                    Log(LOGLEVEL_DEBUG, "DASH Update: insert repid: %s url: %s", (*br)->id.c_str(), bs->url);
                    if ((*brd)->flags_ & Representation::URLSEGMENTS)
                      delete[](*brd)->segments_[0]->url;
                    bs->startPTS_ += ptsOffset;
                    (*brd)->segments_.insert(*bs);
                    if ((*brd)->flags_ & Representation::URLSEGMENTS)
                      bs->url = nullptr;
                    ++(*brd)->startNumber_;
                    --repFreeSegments;
                  }
                  //We have renewed the current segment
                  if (!repFreeSegments && numReplace == currentPos + 1)
                    (*brd)->current_segment_ = nullptr;

                  if (((*brd)->flags_ & Representation::WAITFORSEGMENT) && (*brd)->get_next_segment((*brd)->current_segment_))
                  {
                    (*brd)->flags_ &= ~Representation::WAITFORSEGMENT;
                    Log(LOGLEVEL_DEBUG, "End WaitForSegment stream %s", (*brd)->id.c_str());
                  }

                  if (bs == es)
                    (*brd)->nextPts_ += (*br)->nextPts_;
                  else
                    (*brd)->nextPts_ += bs->startPTS_;
                }
                else if ((*br)->startNumber_ <=1 ) //Full update, be careful with startnumbers!
                {
                  //TODO: check if first element or size differs
                  unsigned int segmentId((*brd)->getCurrentSegmentNumber());
                  if (!(*br)->segments_.empty())
                  {
                    uint64_t searchPts = (*br)->segments_[0]->startPTS_;
                    for (const auto &s : (*brd)->segments_.data)
                    {
                      if (s.startPTS_ >= searchPts)
                        break;
                      ++(*brd)->startNumber_;
                    }
                    (*br)->segments_.swap((*brd)->segments_);
                    if (!~segmentId || segmentId < (*brd)->startNumber_)
                      (*brd)->current_segment_ = nullptr;
                    else
                    {
                      if (segmentId >= (*brd)->startNumber_ + (*brd)->segments_.size())
                        segmentId = (*brd)->startNumber_ + (*brd)->segments_.size() - 1;
                      (*brd)->current_segment_ = (*brd)->get_segment(segmentId - (*brd)->startNumber_);
                    }
                  }

                  if (((*brd)->flags_ & Representation::WAITFORSEGMENT) && (*brd)->get_next_segment((*brd)->current_segment_))
                    (*brd)->flags_ &= ~Representation::WAITFORSEGMENT;

                  Log(LOGLEVEL_DEBUG, "DASH Full update (w/o startnum): repid: %s current_start:%u",
                    (*br)->id.c_str(), (*brd)->startNumber_);
                }
                else if ((*br)->startNumber_ > (*brd)->startNumber_ 
                  || ((*br)->startNumber_ == (*brd)->startNumber_ 
                    && (*br)->segments_.size() > (*brd)->segments_.size()))
                {
                  unsigned int segmentId((*brd)->getCurrentSegmentNumber());
                  (*br)->segments_.swap((*brd)->segments_);
                  (*brd)->startNumber_ = (*br)->startNumber_;
                  if (!~segmentId || segmentId < (*brd)->startNumber_)
                    (*brd)->current_segment_ = nullptr;
                  else
                  {
                    if (segmentId >= (*brd)->startNumber_ + (*brd)->segments_.size())
                      segmentId = (*brd)->startNumber_ + (*brd)->segments_.size() - 1;
                    (*brd)->current_segment_ = (*brd)->get_segment(segmentId - (*brd)->startNumber_);
                  }

                  if (((*brd)->flags_ & Representation::WAITFORSEGMENT) && (*brd)->get_next_segment((*brd)->current_segment_))
                    (*brd)->flags_ &= ~Representation::WAITFORSEGMENT;

                  Log(LOGLEVEL_DEBUG, "DASH Full update (w/ startnum): repid: %s current_start:%u",
                    (*br)->id.c_str(), (*brd)->startNumber_);
                }
              }
            }
          }
        }
      }
    }
  }
}
