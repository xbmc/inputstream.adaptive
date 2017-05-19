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

#include "DASHTree.h"
#include "../oscompat.h"
#include "../helpers.h"

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

static void ParseSegmentTemplate(const char **attr, std::string baseURL, DASHTree::SegmentTemplate &tpl, bool adp)
{
  uint64_t pto(0);
  for (; *attr;)
  {
    if (strcmp((const char*)*attr, "timescale") == 0)
      tpl.timescale = atoi((const char*)*(attr + 1));
    else if (strcmp((const char*)*attr, "duration") == 0)
      tpl.duration = atoi((const char*)*(attr + 1));
    else if (strcmp((const char*)*attr, "media") == 0)
      tpl.media = (const char*)*(attr + 1);
    else if (strcmp((const char*)*attr, "startNumber") == 0)
      tpl.startNumber = atoi((const char*)*(attr + 1));
    else if (strcmp((const char*)*attr, "initialization") == 0)
      tpl.initialization = (const char*)*(attr + 1);
    else if (strcmp((const char*)*attr, "presentationTimeOffset") == 0)
      pto = atoll((const char*)*(attr + 1));
    attr += 2;
  }
  tpl.presentationTimeOffset = tpl.timescale ? (double)pto / tpl.timescale : 0;
  tpl.media = baseURL + tpl.media;
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
    dash->currentNode_ |= DASHTree::MPDNODE_CONTENTPROTECTION;
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
static void XMLCALL
start(void *data, const char *el, const char **attr)
{
  DASHTree *dash(reinterpret_cast<DASHTree*>(data));

  if (dash->currentNode_ & DASHTree::MPDNODE_MPD)
  {
    if (dash->currentNode_ & DASHTree::MPDNODE_PERIOD)
    {
      if (dash->currentNode_ & DASHTree::MPDNODE_ADAPTIONSET)
      {
        if (dash->currentNode_ & DASHTree::MPDNODE_REPRESENTATION)
        {
          if (dash->currentNode_ & DASHTree::MPDNODE_BASEURL)
          {
          }
          else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTLIST)
          {
            DASHTree::Segment seg;
            if (strcmp(el, "SegmentURL") == 0)
            {
              for (; *attr;)
              {
                if (strcmp((const char*)*attr, "mediaRange") == 0)
                {
                  seg.SetRange((const char*)*(attr + 1));
                  break;
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
                attr += 2;
              }
              dash->current_representation_->flags_ |= DASHTree::Representation::INITIALIZATION;
              dash->current_representation_->initialization_ = seg;
            }
            else
              return;
          }
          else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTEMPLATE)
          {
            if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTIMELINE)
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
                    s.range_begin_ = s.range_end_ = ~0;
                    dash->current_representation_->initialization_ = s;
                  }
                  s.range_end_ = dash->current_representation_->segtpl_.startNumber;
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
              }
              else //Failure
              {
                dash->currentNode_ &= ~DASHTree::MPDNODE_SEGMENTTIMELINE;
                dash->current_representation_->timescale_ = 0;
              }
            }
            else if (strcmp(el, "SegmentTimeline") == 0)
            {
              dash->current_representation_->flags_ |= DASHTree::Representation::TIMELINE;
              dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTTIMELINE;
            }
          }
          else if (dash->currentNode_ & DASHTree::MPDNODE_CONTENTPROTECTION)
          {
            if (strcmp(el, "cenc:pssh") == 0)
              dash->currentNode_ |= DASHTree::MPDNODE_PSSH;
          }
          else if (strcmp(el, "AudioChannelConfiguration") == 0)
          {
            dash->current_representation_->channelCount_ = GetChannels(attr);
          }
          else if (strcmp(el, "BaseURL") == 0) //Inside Representation
          {
            dash->strXMLText_.clear();
            dash->currentNode_ |= DASHTree::MPDNODE_BASEURL;
          }
          else if (strcmp(el, "SegmentList") == 0)
          {

            for (; *attr;)
            {
              if (strcmp((const char*)*attr, "duration") == 0)
                dash->current_representation_->duration_ = atoi((const char*)*(attr + 1));
              else if (strcmp((const char*)*attr, "timescale") == 0)
                dash->current_representation_->timescale_ = atoi((const char*)*(attr + 1));
              attr += 2;
            }
            if (dash->current_representation_->timescale_)
            {
              dash->current_representation_->segments_.data.reserve(dash->estimate_segcount(
                dash->current_representation_->duration_,
                dash->current_representation_->timescale_));
              dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTLIST;
            }
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
              dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTLIST;
          }
          else if (strcmp(el, "SegmentTemplate") == 0)
          {
            dash->current_representation_->segtpl_ = dash->current_adaptationset_->segtpl_;

            ParseSegmentTemplate(attr, dash->current_representation_->url_, dash->current_representation_->segtpl_, false);
            dash->current_representation_->flags_ |= DASHTree::Representation::TEMPLATE;
            if (!dash->current_representation_->segtpl_.initialization.empty())
            {
              dash->current_representation_->flags_ |= DASHTree::Representation::INITIALIZATION;
              dash->current_representation_->url_ += dash->current_representation_->segtpl_.initialization;
              dash->current_representation_->timescale_ = dash->current_representation_->segtpl_.timescale;
            }
            dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTTEMPLATE;
          }
          else if (strcmp(el, "ContentProtection") == 0)
          {
            if (!dash->current_representation_->pssh_set_)
            {
              //Mark protected but invalid
              dash->current_representation_->pssh_set_ = 0xFF;
              if (ParseContentProtection(attr, dash))
                dash->current_hasRepURN_ = true;
            }
          }
        }
        else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTEMPLATE)
        {
          if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTIMELINE)
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
            if(dash->current_adaptationset_->segment_durations_.data.empty())
              dash->current_adaptationset_->startPTS_ = t-(dash->base_time_)*dash->current_adaptationset_->timescale_;
            if (d && r)
            {
              for (; r; --r)
                dash->current_adaptationset_->segment_durations_.data.push_back(d);
            }
          }
          else if (strcmp(el, "SegmentTimeline") == 0)
          {
            dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTTIMELINE;
          }
        }
        else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTDURATIONS)
        {
          if (strcmp(el, "S") == 0 && *(const char*)*attr == 'd')
            dash->current_adaptationset_->segment_durations_.data.push_back(atoi((const char*)*(attr + 1)));
        }
        else if (dash->currentNode_ & DASHTree::MPDNODE_CONTENTPROTECTION)
        {
          if (strcmp(el, "cenc:pssh") == 0)
            dash->currentNode_ |= DASHTree::MPDNODE_PSSH;
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
        else if (dash->currentNode_ & DASHTree::MPDNODE_BASEURL)
        {
        }
        else if (strcmp(el, "SegmentTemplate") == 0)
        {
          ParseSegmentTemplate(attr, dash->current_adaptationset_->base_url_, dash->current_adaptationset_->segtpl_, true);
          dash->current_adaptationset_->timescale_ = dash->current_adaptationset_->segtpl_.timescale;
          dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTTEMPLATE;
        }
        else if (strcmp(el, "Representation") == 0)
        {
          dash->current_representation_ = new DASHTree::Representation();
          dash->current_representation_->channelCount_ = dash->adpChannelCount_;
          dash->current_representation_->codecs_ = dash->current_adaptationset_->codecs_;
          dash->current_representation_->url_ = dash->current_adaptationset_->base_url_;
          dash->current_representation_->timescale_ = dash->current_adaptationset_->timescale_;
          dash->current_adaptationset_->repesentations_.push_back(dash->current_representation_);
          dash->current_representation_->width_ = dash->adpwidth_;
          dash->current_representation_->height_ = dash->adpheight_;
          dash->current_representation_->fpsRate_ = dash->adpfpsRate_;
          dash->current_representation_->aspect_ = dash->adpaspect_;
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
            }
            attr += 2;
          }

          if (dash->current_adaptationset_->type_ == DASHTree::SUBTITLE
          && dash->current_adaptationset_->mimeType_ == "application/ttml+xml")
            dash->current_representation_->flags_ |= DASHTree::Representation::SUBTITLESTREAM;

          dash->currentNode_ |= DASHTree::MPDNODE_REPRESENTATION;
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
          dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTDURATIONS;
        }
        else if (strcmp(el, "ContentProtection") == 0)
        {
          if (!dash->adp_pssh_set_)
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
          dash->currentNode_ |= DASHTree::MPDNODE_BASEURL;
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
        dash->current_hasAdpURN_ = false;

        for (; *attr;)
        {
          if (strcmp((const char*)*attr, "contentType") == 0)
            dash->current_adaptationset_->type_ =
            stricmp((const char*)*(attr + 1), "video") == 0 ? DASHTree::VIDEO
            : stricmp((const char*)*(attr + 1), "audio") == 0 ? DASHTree::AUDIO
            : stricmp((const char*)*(attr + 1), "text") == 0 ? DASHTree::SUBTITLE
            : DASHTree::NOTYPE;
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
        dash->segcount_ = 0;
        dash->currentNode_ |= DASHTree::MPDNODE_ADAPTIONSET;
      }
      else if (strcmp(el, "BaseURL") == 0) // Inside Period
      {
        dash->strXMLText_.clear();
        dash->currentNode_ |= DASHTree::MPDNODE_BASEURL;
      }
    }
    else if (strcmp(el, "BaseURL") == 0) // Out of Period
    {
      dash->strXMLText_.clear();
      dash->currentNode_ |= DASHTree::MPDNODE_BASEURL;
    }
    else if (strcmp(el, "Period") == 0)
    {
      dash->current_period_ = new DASHTree::Period();
      dash->current_period_->base_url_ = dash->base_url_;
      dash->periods_.push_back(dash->current_period_);
      dash->currentNode_ |= DASHTree::MPDNODE_PERIOD;
    }
  }
  else if (strcmp(el, "MPD") == 0)
  {
    const char *mpt(0), *tsbd(0), *mpdtype(0);

    dash->overallSeconds_ = 0;
    dash->stream_start_ = time(0);

    for (; *attr;)
    {
      if (strcmp((const char*)*attr, "mediaPresentationDuration") == 0)
        mpt = (const char*)*(attr + 1);
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
      attr += 2;
    }

    if (!~dash->available_time_)
      dash->available_time_ = dash->publish_time_;

    if (!mpt) mpt = tsbd;

    if (mpt && *mpt++ == 'P' && *mpt++ == 'T')
    {
      const char *next = strchr(mpt, 'H');
      if (next){
        dash->overallSeconds_ += static_cast<uint64_t>(atof(mpt)*3600);
        mpt = next + 1;
      }
      next = strchr(mpt, 'M');
      if (next){
        dash->overallSeconds_ += static_cast<uint64_t>(atof(mpt)*60);
        mpt = next + 1;
      }
      next = strchr(mpt, 'S');
      if (next)
        dash->overallSeconds_ += static_cast<uint64_t>(atof(mpt));
    }
    if (dash->publish_time_ && dash->available_time_ && dash->publish_time_ - dash->available_time_ > dash->overallSeconds_)
      dash->base_time_ = dash->publish_time_ - dash->available_time_ - dash->overallSeconds_;
    dash->minPresentationOffset = DBL_MAX;

    dash->currentNode_ |= DASHTree::MPDNODE_MPD;
  }
}

/*----------------------------------------------------------------------
|   expat text
+---------------------------------------------------------------------*/
static void XMLCALL
text(void *data, const char *s, int len)
{
  DASHTree *dash(reinterpret_cast<DASHTree*>(data));
  if (dash->currentNode_ & (DASHTree::MPDNODE_BASEURL | DASHTree::MPDNODE_PSSH))
    dash->strXMLText_ += std::string(s, len);
}

/*----------------------------------------------------------------------
|   expat end
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
end(void *data, const char *el)
{
  DASHTree *dash(reinterpret_cast<DASHTree*>(data));

  if (dash->currentNode_ & DASHTree::MPDNODE_MPD)
  {
    if (dash->currentNode_ & DASHTree::MPDNODE_PERIOD)
    {
      if (dash->currentNode_ & DASHTree::MPDNODE_ADAPTIONSET)
      {
        if (dash->currentNode_ & DASHTree::MPDNODE_REPRESENTATION)
        {
          if (dash->currentNode_ & DASHTree::MPDNODE_BASEURL) // Inside Representation
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
              dash->currentNode_ &= ~DASHTree::MPDNODE_BASEURL;
            }
          }
          else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTLIST)
          {
            if (strcmp(el, "SegmentList") == 0 || strcmp(el, "SegmentBase") == 0)
            {
              dash->currentNode_ &= ~DASHTree::MPDNODE_SEGMENTLIST;
              if (!dash->segcount_)
                dash->segcount_ = dash->current_representation_->segments_.data.size();
            }
          }
          else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTEMPLATE)
          {
            if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTIMELINE)
            {
              if (strcmp(el, "SegmentTimeline") == 0)
                dash->currentNode_ &= ~DASHTree::MPDNODE_SEGMENTTIMELINE;
            }
            else if (strcmp(el, "SegmentTemplate") == 0)
            {
              if (dash->current_representation_->segtpl_.presentationTimeOffset < dash->minPresentationOffset)
                dash->minPresentationOffset = dash->current_representation_->segtpl_.presentationTimeOffset;
              dash->currentNode_ &= ~DASHTree::MPDNODE_SEGMENTTEMPLATE;
            }
          }
          else if (dash->currentNode_ & DASHTree::MPDNODE_CONTENTPROTECTION)
          {
            if (dash->currentNode_ & DASHTree::MPDNODE_PSSH)
            {
              if (strcmp(el, "cenc:pssh") == 0)
              {
                dash->current_pssh_ = dash->strXMLText_;
                dash->currentNode_ &= ~DASHTree::MPDNODE_PSSH;
              }
            }
            else if (strcmp(el, "ContentProtection") == 0)
            {
              if (dash->current_pssh_.empty())
                dash->current_pssh_ = "FILE";
              dash->current_representation_->pssh_set_ = dash->insert_psshset(dash->current_adaptationset_->type_);
              dash->currentNode_ &= ~DASHTree::MPDNODE_CONTENTPROTECTION;
            }
          }
          else if (strcmp(el, "Representation") == 0)
          {
            dash->currentNode_ &= ~DASHTree::MPDNODE_REPRESENTATION;

            if (dash->current_representation_->pssh_set_ == 0xFF)
            {
              // Some manifests dont have Protection per URN included.
              // We treet manifests valid if no single URN was given
              if (!dash->current_hasRepURN_)
              {
                dash->current_pssh_ = "FILE";
                dash->current_representation_->pssh_set_ = dash->insert_psshset(dash->current_adaptationset_->type_);
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
                  seg.range_begin_ = ~0;

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
                  if(timeBased)
                    dash->current_representation_->flags_ |= DASHTree::Representation::TIMETEMPLATE;

                  seg.range_end_ = timeBased ? dash->current_adaptationset_->startPTS_ : tpl.startNumber;
                  seg.startPTS_ = dash->current_adaptationset_->startPTS_;

                  if (!timeBased && dash->available_time_ && dash->stream_start_ - dash->available_time_ > dash->overallSeconds_) //we need to adjust the start-segment
                    seg.range_end_ += static_cast<uint64_t>(((dash->stream_start_ - dash->available_time_ - dash->overallSeconds_)*tpl.timescale) / tpl.duration);

                  for (;countSegs;--countSegs)
                  {
                    dash->current_representation_->segments_.data.push_back(seg);
                    seg.startPTS_ += (sdb != sde) ? *sdb : tpl.duration;
                    seg.range_end_ += timeBased ? *(sdb++) : 1;
                  }
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
          }
        }
        else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTDURATIONS)
        {
          if (strcmp(el, "SegmentDurations") == 0)
            dash->currentNode_ &= ~DASHTree::MPDNODE_SEGMENTDURATIONS;
        }
        else if (dash->currentNode_ & DASHTree::MPDNODE_BASEURL) // Inside AdaptationSet
        {
          if (strcmp(el, "BaseURL") == 0)
          {
            dash->current_adaptationset_->base_url_ = dash->current_period_->base_url_ + dash->strXMLText_;
            dash->currentNode_ &= ~DASHTree::MPDNODE_BASEURL;
          }
        }
        else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTEMPLATE)
        {
          if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTIMELINE)
          {
            if (strcmp(el, "SegmentTimeline") == 0)
              dash->currentNode_ &= ~DASHTree::MPDNODE_SEGMENTTIMELINE;
          }
          else if (strcmp(el, "SegmentTemplate") == 0)
          {
            if (dash->current_adaptationset_->segtpl_.presentationTimeOffset < dash->minPresentationOffset)
              dash->minPresentationOffset = dash->current_adaptationset_->segtpl_.presentationTimeOffset;
            dash->currentNode_ &= ~DASHTree::MPDNODE_SEGMENTTEMPLATE;
          }
        }
        else if (dash->currentNode_ & DASHTree::MPDNODE_CONTENTPROTECTION)
        {
          if (dash->currentNode_ & DASHTree::MPDNODE_PSSH)
          {
            if (strcmp(el, "cenc:pssh") == 0)
            {
              dash->current_pssh_ = dash->strXMLText_;
              dash->currentNode_ &= ~DASHTree::MPDNODE_PSSH;
            }
          }
          else if (strcmp(el, "ContentProtection") == 0)
          {
            if (dash->current_pssh_.empty())
              dash->current_pssh_ = "FILE";
            dash->adp_pssh_set_ = dash->insert_psshset(dash->current_adaptationset_->type_);
            dash->currentNode_ &= ~DASHTree::MPDNODE_CONTENTPROTECTION;
          }
        }
        else if (strcmp(el, "AdaptationSet") == 0)
        {
          dash->currentNode_ &= ~DASHTree::MPDNODE_ADAPTIONSET;
          if (dash->current_adaptationset_->type_ == DASHTree::NOTYPE
          || dash->adp_pssh_set_ == 0xFF
          || dash->current_adaptationset_->repesentations_.empty())
          {
            delete dash->current_adaptationset_;
            dash->current_period_->adaptationSets_.pop_back();
          }
          else
          {
            if (dash->adp_pssh_set_)
            {
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
              }
            }
          }
        }
      }
      else if (dash->currentNode_ & DASHTree::MPDNODE_BASEURL) // Inside Period
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
          dash->currentNode_ &= ~DASHTree::MPDNODE_BASEURL;
        }
      }
      else if (strcmp(el, "Period") == 0)
      {
        dash->currentNode_ &= ~DASHTree::MPDNODE_PERIOD;
      }
    }
    else if (dash->currentNode_ & DASHTree::MPDNODE_BASEURL) // Outside Period
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
        dash->currentNode_ &= ~DASHTree::MPDNODE_BASEURL;
      }
    }
    else if (strcmp(el, "MPD") == 0)
    {
      dash->currentNode_ &= ~DASHTree::MPDNODE_MPD;
    }
  }
}

/*----------------------------------------------------------------------
|   DASHTree
+---------------------------------------------------------------------*/

bool DASHTree::open(const char *url)
{
  parser_ = XML_ParserCreate(NULL);
  if (!parser_)
    return false;
  XML_SetUserData(parser_, (void*)this);
  XML_SetElementHandler(parser_, start, end);
  XML_SetCharacterDataHandler(parser_, text);
  currentNode_ = 0;
  strXMLText_.clear();

  bool ret = download(url, manifest_headers_);
  
  XML_ParserFree(parser_);
  parser_ = 0;

  SortRepresentations();

  return ret;
}

bool DASHTree::write_data(void *buffer, size_t buffer_size)
{
  bool done(false);
  XML_Status retval = XML_Parse(parser_, (const char*)buffer, buffer_size, done);

  if (retval == XML_STATUS_ERROR)
  {
    unsigned int byteNumber = XML_GetErrorByteIndex(parser_);
    return false;
  }
  return true;
}
