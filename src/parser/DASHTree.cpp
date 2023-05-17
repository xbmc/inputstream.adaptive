/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DASHTree.h"

#include "../oscompat.h"
#include "../utils/StringUtils.h"
#include "../utils/UrlUtils.h"
#include "../utils/Utils.h"
#include "../utils/log.h"
#include "PRProtectionParser.h"
#include "kodi/tools/StringUtils.h"

#include <cstring>
#include <float.h>
#include <string>
#include <thread>
#include <time.h>

using namespace adaptive;
using namespace UTILS;
using namespace kodi::tools;

enum
{
  MPDNODE_MPD = 1 << 0,
  MPDNODE_LOCATION = 1 << 1,
  MPDNODE_PERIOD = 1 << 2,
  MPDNODE_ADAPTIONSET = 1 << 3,
  MPDNODE_CONTENTPROTECTION = 1 << 4,
  MPDNODE_REPRESENTATION = 1 << 5,
  MPDNODE_BASEURL = 1 << 6,
  MPDNODE_SEGMENTLIST = 1 << 7,
  MPDNODE_INITIALIZATION = 1 << 8,
  MPDNODE_SEGMENTURL = 1 << 9,
  MPDNODE_SEGMENTDURATIONS = 1 << 10,
  MPDNODE_S = 1 << 11,
  MPDNODE_PSSH = 1 << 12,
  MPDNODE_SEGMENTTEMPLATE = 1 << 13,
  MPDNODE_SEGMENTTIMELINE = 1 << 14,
  MPDNODE_ROLE = 1 << 15,
  MPDNODE_PLAYREADYWRMHEADER = 1 << 16
};

static const char* CONTENTPROTECTION_TAG = "ContentProtection";

static uint8_t GetChannels(const char** attr)
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
    if (strcmp(schemeIdUri, "urn:mpeg:dash:23003:3:audio_channel_configuration:2011") == 0 ||
             strcmp(schemeIdUri, "urn:mpeg:mpegB:cicp:ChannelConfiguration") == 0)
      return atoi(value);
    else if (strcmp(schemeIdUri, "urn:dolby:dash:audio_channel_configuration:2011") == 0 ||
             strcmp(schemeIdUri, "tag:dolby.com,2014:dash:audio_channel_configuration:2011") == 0)
    {
      if (strcmp(value, "F801") == 0)
        return 6;
      else if (strcmp(value, "FE01") == 0)
        return 8;
    }
  }
  return 0;
}

static uint64_t ParseSegmentTemplate(const char** attr,
                                         std::string baseURL,
                                         DASHTree::SegmentTemplate& tpl,
                                         uint64_t startNumber)
{
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

  if (!tpl.timescale) // if not specified timescale defaults to seconds
    tpl.timescale = 1;

  if (URL::IsUrlRelative(tpl.media))
  {
    tpl.media = URL::Join(baseURL, tpl.media);
  }

  if (URL::IsUrlRelative(tpl.initialization))
  {
    tpl.initialization = URL::Join(baseURL, tpl.initialization);
  }
  return startNumber;
}

static time_t getTime(const char* timeStr)
{
  int year, mon, day, hour, minu, sec;
  if (sscanf(timeStr, "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &hour, &minu, &sec) == 6)
  {
    struct tm tmd;

    memset(&tmd, 0, sizeof(tmd));
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
  if (dur && dur[0] == 'P')
  {
    ++dur;
    const char* next = strchr(dur, 'D');
    if (next)
    {
      retVal += static_cast<uint64_t>(atof(dur) * 86400 * scale);
      dur = next + 1;
    }

    dur = strchr(dur, 'T');
    if (dur)
      ++dur;
    else
      return;

    next = strchr(dur, 'H');
    if (next)
    {
      retVal += static_cast<uint64_t>(atof(dur) * 3600 * scale);
      dur = next + 1;
    }
    next = strchr(dur, 'M');
    if (next)
    {
      retVal += static_cast<uint64_t>(atof(dur) * 60 * scale);
      dur = next + 1;
    }
    next = strchr(dur, 'S');
    if (next)
      retVal += static_cast<uint64_t>(atof(dur) * scale);
  }
}

bool ParseContentProtection(const char** attr, DASHTree* dash)
{
  dash->strXMLText_.clear();
  dash->current_period_->encryptionState_ |= DASHTree::ENCRYTIONSTATE_ENCRYPTED;
  bool urnFound(false), mpdFound(false);
  const char* defaultKID(0);
  for (; *attr;)
  {
    if (strcmp((const char*)*attr, "schemeIdUri") == 0)
    {
      if (strcmp((const char*)*(attr + 1), "urn:mpeg:dash:mp4protection:2011") == 0)
        mpdFound = true;
      else
        urnFound = stricmp(dash->m_supportedKeySystem.c_str(), (const char*)*(attr + 1)) == 0;
    }
    else if (StringUtils::EqualsNoCase(*attr, "value"))
    {
      std::string_view protectionValue = *(attr + 1);
      if (protectionValue == "cenc")
        dash->m_cryptoMode = CryptoMode::AES_CTR;
      else if (protectionValue == "cbcs")
        dash->m_cryptoMode = CryptoMode::AES_CBC;
    }
    else if (StringUtils::EndsWith(*attr, "default_KID"))
      defaultKID = (const char*)*(attr + 1);
    attr += 2;
  }
  if (urnFound)
  {
    dash->currentNode_ |= MPDNODE_CONTENTPROTECTION;
    dash->current_period_->encryptionState_ |= DASHTree::ENCRYTIONSTATE_SUPPORTED;
  }
  if ((urnFound || mpdFound) && defaultKID && strlen(defaultKID) == 36)
  {
    dash->current_defaultKID_.resize(16);
    for (unsigned int i(0); i < 16; ++i)
    {
      if (i == 4 || i == 6 || i == 8 || i == 10)
        ++defaultKID;
      dash->current_defaultKID_[i] = STRING::ToHexNibble(*defaultKID) << 4;
      ++defaultKID;
      dash->current_defaultKID_[i] |= STRING::ToHexNibble(*defaultKID);
      ++defaultKID;
    }
  }
  // Return if we have URN or not
  return urnFound || !mpdFound;
}

/*----------------------------------------------------------------------
|   expat start
+---------------------------------------------------------------------*/

static void ReplacePlaceHolders(std::string& rep, const std::string& id, uint32_t bandwidth)
{
  STRING::ReplaceAll(rep, "$RepresentationID$", id);
  STRING::ReplaceAll(rep, "$Bandwidth$", std::to_string(bandwidth));
}

static void XMLCALL start(void* data, const char* el, const char** attr)
{
  DASHTree* dash(reinterpret_cast<DASHTree*>(data));

  if (dash->currentNode_ & MPDNODE_MPD)
  {
    if (dash->currentNode_ & MPDNODE_LOCATION)
    {
    }
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
            seg.range_begin_ = ~0ULL;
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
                  seg.url = std::string{*(attr + 1), sz};

                  if (dash->current_representation_->segments_.data.empty())
                    seg.range_end_ = dash->current_representation_->startNumber_;
                }
                attr += 2;
              }

              if (dash->current_representation_->segments_.data.empty())
                seg.startPTS_ = dash->base_time_ + dash->current_representation_->ptsOffset_;
              else
                seg.startPTS_ = dash->current_representation_->nextPts_ +
                                dash->current_representation_->duration_;

              dash->current_representation_->nextPts_ = seg.startPTS_;
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
                  seg.url = std::string{*(attr + 1), sz};
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

              for (; *attr;)
              {
                if (strcmp((const char*)*attr, "t") == 0)
                  dash->timeline_time_ = atoll((const char*)*(attr + 1));
                else if (strcmp((const char*)*attr, "d") == 0)
                  d = atoi((const char*)*(attr + 1));
                if (strcmp((const char*)*attr, "r") == 0)
                  r = atoi((const char*)*(attr + 1)) + 1;
                attr += 2;
              }
              if (d && r)
              {
                DASHTree::Segment s;
                if (dash->current_representation_->segments_.data.empty())
                {
                  uint64_t overallSeconds =
                      dash->current_period_->duration_
                          ? dash->current_period_->duration_ / dash->current_period_->timescale_
                          : dash->overallSeconds_;
                  if (dash->current_representation_->segtpl_.duration &&
                      dash->current_representation_->segtpl_.timescale)
                    dash->current_representation_->segments_.data.reserve(
                        (unsigned int)((double)overallSeconds /
                                       (((double)dash->current_representation_->segtpl_.duration) /
                                        dash->current_representation_->segtpl_.timescale)) +
                        1);

                  if (dash->current_representation_->flags_ &
                      DASHTree::Representation::INITIALIZATION)
                  {
                    s.range_begin_ = 0ULL, s.range_end_ = 0;
                    dash->current_representation_->initialization_ = s;
                  }
                  s.range_end_ = dash->current_representation_->startNumber_;
                }
                else
                  s.range_end_ =
                      dash->current_representation_->segments_.data.back().range_end_ + 1;
                s.range_begin_ = s.startPTS_ = dash->timeline_time_;
                s.startPTS_ -= dash->base_time_ * dash->current_representation_->segtpl_.timescale;

                for (; r; --r)
                {
                  dash->current_representation_->segments_.data.push_back(s);
                  ++s.range_end_;
                  s.range_begin_ = (dash->timeline_time_ += d);
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

              if (dash->m_manifestUpdateParam.empty() && dash->has_timeshift_buffer_)
                dash->m_manifestUpdateParam = "full";
            }
          }
          else if (dash->currentNode_ & MPDNODE_CONTENTPROTECTION)
          {
            if (StringUtils::EndsWith(el, "pssh"))
              dash->currentNode_ |= MPDNODE_PSSH;
            else if (strcmp(el, "widevine:license") == 0)
            {
              for (; *attr;)
              {
                if (strcmp((const char*)*attr, "robustness_level") == 0)
                  dash->current_period_->need_secure_decoder_ =
                      strncmp((const char*)*(attr + 1), "HW", 2) == 0;
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
            uint32_t dur(0), ts(1), pto(0), sn(0);
            for (; *attr;)
            {
              if (strcmp((const char*)*attr, "duration") == 0)
                dur = atoi((const char*)*(attr + 1));
              else if (strcmp((const char*)*attr, "timescale") == 0)
                ts = atoi((const char*)*(attr + 1));
              else if (strcmp((const char*)*attr, "presentationTimeOffset") == 0)
                pto = atoi((const char*)*(attr + 1));
              else if (strcmp((const char*)*attr, "startNumber") == 0)
                sn = atoi((const char*)*(attr + 1));
              attr += 2;
            }
            if (sn)
            {
              dash->current_representation_->startNumber_ = sn;
              pto += sn * dur;
            }
            if (pto)
              dash->current_representation_->ptsOffset_ = pto;
            if (ts && dur)
            {
              dash->current_representation_->duration_ = dur;
              dash->current_representation_->timescale_ = ts;
              dash->current_representation_->segments_.data.reserve(
                  dash->EstimateSegmentsCount(dash->current_representation_->duration_,
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
                sscanf((const char*)*(attr + 1), "%u-%u",
                       &dash->current_representation_->indexRangeMin_,
                       &dash->current_representation_->indexRangeMax_);
              else if (strcmp((const char*)*attr, "indexRangeExact") == 0 &&
                       strcmp((const char*)*(attr + 1), "true") == 0)
                dash->current_representation_->flags_ |= DASHTree::Representation::INDEXRANGEEXACT;
              attr += 2;
            }
            dash->current_representation_->flags_ |= DASHTree::Representation::SEGMENTBASE;
            if (dash->current_representation_->indexRangeMax_)
              dash->currentNode_ |= MPDNODE_SEGMENTLIST;
          }
          else if (strcmp(el, "SegmentTemplate") == 0) // Inside Representation
          {
            dash->current_representation_->segtpl_ = dash->current_adaptationset_->segtpl_;

            dash->current_representation_->startNumber_ = ParseSegmentTemplate(
                attr, dash->current_representation_->base_url_,
                dash->current_representation_->segtpl_, dash->current_adaptationset_->startNumber_);
            dash->current_representation_->segtpl_.media_url =
                dash->current_representation_->segtpl_.media;
            ReplacePlaceHolders(dash->current_representation_->segtpl_.media_url,
                                dash->current_representation_->id,
                                dash->current_representation_->bandwidth_);
            dash->current_representation_->flags_ |= DASHTree::Representation::TEMPLATE;
            if (!dash->current_representation_->segtpl_.initialization.empty())
            {
              ReplacePlaceHolders(dash->current_representation_->segtpl_.initialization,
                                  dash->current_representation_->id,
                                  dash->current_representation_->bandwidth_);
              dash->current_representation_->flags_ |= DASHTree::Representation::INITIALIZATION;
              dash->current_representation_->url_ =
                  dash->current_representation_->segtpl_.initialization;
              dash->current_representation_->timescale_ =
                  dash->current_representation_->segtpl_.timescale;
            }
            dash->timeline_time_ = 0;
            dash->currentNode_ |= MPDNODE_SEGMENTTEMPLATE;
          }
          else if (strcmp(el, CONTENTPROTECTION_TAG) == 0)
          {
            if (!dash->current_representation_->pssh_set_ ||
                dash->current_representation_->pssh_set_ == 0xFF)
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
                r = atoi((const char*)*(attr + 1)) + 1;
              attr += 2;
            }
            if (dash->current_adaptationset_->segment_durations_.data.empty())
              dash->current_adaptationset_->startPTS_ = dash->pts_helper_ = t;
            else if (t)
            {
              //Go back to the previous timestamp to calculate the real gap.
              dash->pts_helper_ -= dash->current_adaptationset_->segment_durations_.data.back();
              dash->current_adaptationset_->segment_durations_.data.back() =
                  static_cast<uint32_t>(t - dash->pts_helper_);
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

            if (dash->m_manifestUpdateParam.empty() && dash->has_timeshift_buffer_)
              dash->m_manifestUpdateParam = "full";
          }
        }
        else if (dash->currentNode_ & MPDNODE_SEGMENTDURATIONS)
        {
          if (strcmp(el, "S") == 0 && *(const char*)*attr == 'd')
            dash->current_adaptationset_->segment_durations_.data.push_back(
                atoi((const char*)*(attr + 1)));
        }
        else if (dash->currentNode_ & MPDNODE_CONTENTPROTECTION)
        {
          if (StringUtils::EndsWith(el, "pssh"))
            dash->currentNode_ |= MPDNODE_PSSH;
          else if (strcmp(el, "widevine:license") == 0)
          {
            for (; *attr;)
            {
              if (strcmp((const char*)*attr, "robustness_level") == 0)
                dash->current_period_->need_secure_decoder_ =
                    strncmp((const char*)*(attr + 1), "HW", 2) == 0;
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
                  stricmp((const char*)*(attr + 1), "video") == 0
                      ? DASHTree::VIDEO
                      : stricmp((const char*)*(attr + 1), "audio") == 0
                            ? DASHTree::AUDIO
                            : stricmp((const char*)*(attr + 1), "text") == 0 ? DASHTree::SUBTITLE
                                                                             : DASHTree::NOTYPE;
              break;
            }
            attr += 2;
          }
        }
        else if (strcmp(el, "SupplementalProperty") == 0)
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
            if (strcmp(schemeIdUri, "urn:mpeg:dash:adaptation-set-switching:2016") == 0)
              dash->current_adaptationset_->switching_ids_ = StringUtils::Split(value, ',');
          }
        }
        else if (dash->currentNode_ & MPDNODE_BASEURL)
        {
        }
        else if (strcmp(el, "SegmentTemplate") == 0) // Inside Adaptation set
        {
          dash->current_adaptationset_->startNumber_ = ParseSegmentTemplate(
              attr, dash->current_adaptationset_->base_url_, dash->current_adaptationset_->segtpl_,
              dash->current_adaptationset_->startNumber_);
          dash->current_adaptationset_->timescale_ =
              dash->current_adaptationset_->segtpl_.timescale;
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
        else if (strcmp(el, "Role") == 0)
        {
          bool schemeOk = false;
          const char* value = nullptr;
          for (; *attr;)
          {
            if (strcmp((const char*)*attr, "schemeIdUri") == 0)
            {
              if (strcmp((const char*)*(attr + 1), "urn:mpeg:dash:role:2011") == 0)
                schemeOk = true;
            }
            else if (strcmp((const char*)*attr, "value") == 0)
              value = (const char*)*(attr + 1);
            attr += 2;
          }
          if (schemeOk && value)
          {
            if (strcmp(value, "subtitle") == 0)
              dash->current_adaptationset_->type_ = DASHTree::SUBTITLE;
            //Legacy compatibility
            if (strcmp(value, "forced") == 0)
              dash->current_adaptationset_->forced_ = true;
            if (strcmp(value, "main") == 0)
              dash->current_adaptationset_->default_ = true;
          }
        }
        else if (strcmp(el, "Representation") == 0)
        {
          dash->current_representation_ = new AdaptiveTree::Representation();
          dash->current_representation_->channelCount_ = dash->adpChannelCount_;
          dash->current_representation_->codecs_ = dash->current_adaptationset_->codecs_;
          dash->current_representation_->url_ = dash->current_adaptationset_->base_url_;
          dash->current_representation_->timescale_ = dash->current_adaptationset_->timescale_;
          dash->current_representation_->duration_ = dash->current_adaptationset_->duration_;
          dash->current_representation_->startNumber_ = dash->current_adaptationset_->startNumber_;
          dash->current_representation_->width_ = dash->adpwidth_;
          dash->current_representation_->height_ = dash->adpheight_;
          dash->current_representation_->fpsRate_ = dash->adpfpsRate_;
          dash->current_representation_->fpsScale_ = dash->adpfpsScale_;
          dash->current_representation_->aspect_ = dash->adpaspect_;
          dash->current_representation_->containerType_ = dash->adpContainerType_;
          dash->current_representation_->base_url_ = dash->current_adaptationset_->base_url_;
          dash->current_representation_->assured_buffer_duration_ =
              dash->m_settings.m_bufferAssuredDuration;
          dash->current_representation_->max_buffer_duration_ =
              dash->m_settings.m_bufferMaxDuration;

          dash->current_adaptationset_->representations_.push_back(dash->current_representation_);

          dash->current_pssh_.clear();
          dash->current_hasRepURN_ = false;

          for (; *attr;)
          {
            if (strcmp((const char*)*attr, "bandwidth") == 0)
              dash->current_representation_->bandwidth_ = atoi((const char*)*(attr + 1));
            else if (strcmp((const char*)*attr, "codecs") == 0)
              dash->current_representation_->codecs_ = (const char*)*(attr + 1);
            else if (strcmp((const char*)*attr, "width") == 0)
              dash->current_representation_->width_ = std::atoi((const char*)*(attr + 1));
            else if (strcmp((const char*)*attr, "height") == 0)
              dash->current_representation_->height_ = std::atoi((const char*)*(attr + 1));
            else if (strcmp((const char*)*attr, "audioSamplingRate") == 0)
              dash->current_representation_->samplingRate_ =
                  static_cast<uint32_t>(atoi((const char*)*(attr + 1)));
            else if (strcmp((const char*)*attr, "frameRate") == 0)
            {
              dash->current_representation_->fpsScale_ = 1;
              sscanf((const char*)*(attr + 1), "%" SCNu32 "/%" SCNu32,
                     &dash->current_representation_->fpsRate_,
                     &dash->current_representation_->fpsScale_);
            }
            else if (strcmp((const char*)*attr, "id") == 0)
              dash->current_representation_->id = (const char*)*(attr + 1);
            else if (strcmp((const char*)*attr, "codecPrivateData") == 0)
              dash->current_representation_->codec_private_data_ =
                  AnnexbToAvc((const char*)*(attr + 1));
            else if (strcmp((const char*)*attr, "hdcp") == 0)
              dash->current_representation_->hdcpVersion_ =
                  static_cast<uint16_t>(atof((const char*)*(attr + 1)) * 10);
            else if (dash->current_adaptationset_->mimeType_.empty() &&
                     strcmp((const char*)*attr, "mimeType") == 0)
            {
              dash->current_adaptationset_->mimeType_ = (const char*)*(attr + 1);
              if (dash->current_adaptationset_->type_ == DASHTree::NOTYPE)
              {
                if (strncmp(dash->current_adaptationset_->mimeType_.c_str(), "video", 5) == 0)
                  dash->current_adaptationset_->type_ = DASHTree::VIDEO;
                else if (strncmp(dash->current_adaptationset_->mimeType_.c_str(), "audio", 5) == 0)
                  dash->current_adaptationset_->type_ = DASHTree::AUDIO;
                else if (strncmp(dash->current_adaptationset_->mimeType_.c_str(), "application",
                                 11) == 0 ||
                         strncmp(dash->current_adaptationset_->mimeType_.c_str(), "text", 4) == 0)
                  dash->current_adaptationset_->type_ = DASHTree::SUBTITLE;
              }
              if (strstr(dash->current_adaptationset_->mimeType_.c_str(), "/webm"))
                dash->current_representation_->containerType_ = AdaptiveTree::CONTAINERTYPE_WEBM;
              else if (strstr(dash->current_adaptationset_->mimeType_.c_str(), "/x-matroska"))
                dash->current_representation_->containerType_ =
                    AdaptiveTree::CONTAINERTYPE_MATROSKA;
            }
            attr += 2;
          }

          if (dash->current_representation_->codecs_.empty())
          {
            if (dash->current_adaptationset_->mimeType_ == "text/vtt")
              dash->current_representation_->codecs_ = "wvtt";
            else if (dash->current_adaptationset_->mimeType_ == "application/ttml+xml")
              dash->current_representation_->codecs_ = "ttml";
          }

          if (dash->current_adaptationset_->type_ != DASHTree::SUBTITLE)
          {
            if (dash->current_representation_->codecs_ == "wvtt")
            {
              dash->current_adaptationset_->type_ = DASHTree::SUBTITLE;
              dash->current_adaptationset_->mimeType_ = "text/vtt";
            }
            else if (dash->current_representation_->codecs_ == "ttml")
            {
              dash->current_adaptationset_->type_ = DASHTree::SUBTITLE;
              dash->current_adaptationset_->mimeType_ = "application/ttml+xml";
            }
          }

          if (dash->current_adaptationset_->type_ == DASHTree::SUBTITLE &&
              (dash->current_adaptationset_->mimeType_ == "application/ttml+xml" ||
               dash->current_adaptationset_->mimeType_ == "text/vtt"))
          {
            if (dash->current_adaptationset_->segment_durations_.empty())
              dash->current_representation_->flags_ |= DASHTree::Representation::SUBTITLESTREAM;
            else
              dash->current_representation_->containerType_ = AdaptiveTree::CONTAINERTYPE_TEXT;
          }

          dash->current_representation_->segtpl_ = dash->current_adaptationset_->segtpl_;

          if (!dash->current_adaptationset_->segtpl_.media.empty())
          {
            dash->current_representation_->flags_ |= DASHTree::Representation::TEMPLATE;

            dash->current_representation_->segtpl_.media_url =
                dash->current_representation_->segtpl_.media;

            ReplacePlaceHolders(dash->current_representation_->segtpl_.media_url,
                                dash->current_representation_->id,
                                dash->current_representation_->bandwidth_);

            if (!dash->current_representation_->segtpl_.initialization.empty())
            {
              dash->current_representation_->flags_ |= DASHTree::Representation::INITIALIZATION;
              ReplacePlaceHolders(dash->current_representation_->segtpl_.initialization,
                                  dash->current_representation_->id,
                                  dash->current_representation_->bandwidth_);
              dash->current_representation_->url_ =
                  dash->current_representation_->segtpl_.initialization;
            }
          }
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
        else if (strcmp(el, CONTENTPROTECTION_TAG) == 0)
        {
          if (!dash->adp_pssh_set_ || dash->adp_pssh_set_ == 0xFF)
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
        else if (strcmp(el, "mspr:pro") == 0)
        {
          dash->strXMLText_.clear();
          dash->currentNode_ |= MPDNODE_PLAYREADYWRMHEADER;
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
              dash->current_period_->segment_durations_.data.reserve(
                  dash->EstimateSegmentsCount(d, dash->current_period_->timescale_));
            dash->current_period_->startPTS_ = dash->pts_helper_ = t;
          }
          else if (t)
          {
            //Go back to the previous timestamp to calculate the real gap.
            dash->pts_helper_ -= dash->current_period_->segment_durations_.data.back();
            dash->current_period_->segment_durations_.data.back() =
                static_cast<uint32_t>(t - dash->pts_helper_);
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
        dash->adpfpsScale_ = 1;
        dash->adpaspect_ = 0.0f;
        dash->adp_pssh_set_ = 0;
        dash->adpContainerType_ = AdaptiveTree::CONTAINERTYPE_MP4;
        dash->current_hasAdpURN_ = false;
        dash->adp_timelined_ = dash->period_timelined_;
        dash->current_adaptationset_->timescale_ = dash->current_period_->timescale_;
        dash->current_adaptationset_->duration_ = dash->current_period_->duration_;
        dash->current_adaptationset_->segment_durations_ =
            dash->current_period_->segment_durations_;
        dash->current_adaptationset_->segtpl_ = dash->current_period_->segtpl_;
        dash->current_adaptationset_->startNumber_ = dash->current_period_->startNumber_;
        dash->current_playready_wrmheader_.clear();

        for (; *attr;)
        {
          if (strcmp((const char*)*attr, "contentType") == 0)
            dash->current_adaptationset_->type_ =
                stricmp((const char*)*(attr + 1), "video") == 0
                    ? DASHTree::VIDEO
                    : stricmp((const char*)*(attr + 1), "audio") == 0
                          ? DASHTree::AUDIO
                          : stricmp((const char*)*(attr + 1), "text") == 0 ? DASHTree::SUBTITLE
                                                                           : DASHTree::NOTYPE;
          else if (strcmp((const char*)*attr, "id") == 0)
            dash->current_adaptationset_->id_ = (const char*)*(attr + 1);
          else if (strcmp((const char*)*attr, "group") == 0)
            dash->current_adaptationset_->group_ = (const char*)*(attr + 1);
          else if (strcmp((const char*)*attr, "lang") == 0)
            dash->current_adaptationset_->language_ = (const char*)*(attr + 1);
          else if (strcmp((const char*)*attr, "mimeType") == 0)
            dash->current_adaptationset_->mimeType_ = (const char*)*(attr + 1);
          else if (strcmp((const char*)*attr, "name") == 0)
            dash->current_adaptationset_->name_ = (const char*)*(attr + 1);
          else if (strcmp((const char*)*attr, "codecs") == 0)
            dash->current_adaptationset_->codecs_ = (const char*)*(attr + 1);
          else if (strcmp((const char*)*attr, "width") == 0)
            dash->adpwidth_ = static_cast<uint16_t>(atoi((const char*)*(attr + 1)));
          else if (strcmp((const char*)*attr, "height") == 0)
            dash->adpheight_ = static_cast<uint16_t>(atoi((const char*)*(attr + 1)));
          else if (strcmp((const char*)*attr, "frameRate") == 0)
            sscanf((const char*)*(attr + 1), "%" SCNu32 "/%" SCNu32,
                    &dash->adpfpsRate_,
                    &dash->adpfpsScale_);
          else if (strcmp((const char*)*attr, "par") == 0)
          {
            int w, h;
            if (sscanf((const char*)*(attr + 1), "%d:%d", &w, &h) == 2)
              dash->adpaspect_ = (float)w / h;
          }
          else if (strcmp((const char*)*attr, "audioTrackId") == 0)
            dash->current_adaptationset_->audio_track_id_ = (const char*)*(attr + 1);
          else if (strcmp((const char*)*attr, "impaired") == 0)
            dash->current_adaptationset_->impaired_ = strcmp((const char*)*(attr + 1), "true") == 0;
          else if (strcmp((const char*)*attr, "forced") == 0)
            dash->current_adaptationset_->forced_ = strcmp((const char*)*(attr + 1), "true") == 0;
          else if (strcmp((const char*)*attr, "original") == 0)
            dash->current_adaptationset_->original_ = strcmp((const char*)*(attr + 1), "true") == 0;
          else if (strcmp((const char*)*attr, "default") == 0)
            dash->current_adaptationset_->default_ = strcmp((const char*)*(attr + 1), "true") == 0;
          attr += 2;
        }

        if (dash->current_adaptationset_->type_ == DASHTree::NOTYPE)
        {
          if (strncmp(dash->current_adaptationset_->mimeType_.c_str(), "video", 5) == 0)
            dash->current_adaptationset_->type_ = DASHTree::VIDEO;
          else if (strncmp(dash->current_adaptationset_->mimeType_.c_str(), "audio", 5) == 0)
            dash->current_adaptationset_->type_ = DASHTree::AUDIO;
          else if (strncmp(dash->current_adaptationset_->mimeType_.c_str(), "application", 11) ==
                       0 ||
                   strncmp(dash->current_adaptationset_->mimeType_.c_str(), "text", 4) == 0)
            dash->current_adaptationset_->type_ = DASHTree::SUBTITLE;
        }

        if (strstr(dash->current_adaptationset_->mimeType_.c_str(), "/webm"))
          dash->adpContainerType_ = AdaptiveTree::CONTAINERTYPE_WEBM;
        else if (strstr(dash->current_adaptationset_->mimeType_.c_str(), "/x-matroska"))
          dash->adpContainerType_ = AdaptiveTree::CONTAINERTYPE_MATROSKA;

        dash->segcount_ = 0;
        dash->currentNode_ |= MPDNODE_ADAPTIONSET;
      }
      else if (strcmp(el, "SegmentTemplate") == 0) // Inside Period
      {
        dash->current_period_->startNumber_ = ParseSegmentTemplate(
            attr, dash->current_period_->base_url_, dash->current_period_->segtpl_,
            dash->current_period_->startNumber_);
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
            dash->current_period_->segment_durations_.data.reserve(dash->EstimateSegmentsCount(
                dash->current_period_->duration_, dash->current_period_->timescale_));
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
      dash->current_period_->base_url_ = dash->mpd_url_;
      dash->periods_.push_back(dash->current_period_);
      dash->period_timelined_ = false;
      dash->current_period_->start_ = 0;
      dash->current_period_->sequence_ = dash->current_sequence_++;

      for (; *attr;)
      {
        if (strcmp((const char*)*attr, "start") == 0)
          AddDuration((const char*)*(attr + 1), dash->current_period_->start_, 1000);
        else if (strcmp((const char*)*attr, "id") == 0)
          dash->current_period_->id_ = (const char*)*(attr + 1);
        else if (strcmp((const char*)*attr, "duration") == 0)
          AddDuration((const char*)*(attr + 1), dash->current_period_->duration_, 1000);
        attr += 2;
      }
      dash->currentNode_ |= MPDNODE_PERIOD;
    }
    else if (strcmp(el, "Location") == 0)
    {
      dash->strXMLText_.clear();
      dash->currentNode_ |= MPDNODE_LOCATION;
    }
  }
  else if (strcmp(el, "MPD") == 0)
  {
    const char *mpt(0), *tsbd(0);

    dash->firstStartNumber_ = 0;
    dash->current_sequence_ = 0;
    dash->overallSeconds_ = 0;
    dash->stream_start_ = dash->GetNowTime();
    dash->mpd_url_ = dash->base_url_;

    for (; *attr;)
    {
      if (strcmp((const char*)*attr, "mediaPresentationDuration") == 0)
      {
        mpt = (const char*)*(attr + 1);
      }
      else if (strcmp((const char*)*attr, "type") == 0 &&
               strcmp((const char*)*(attr + 1), "dynamic") == 0)
      {
        dash->has_timeshift_buffer_ = true;
      }
      else if (strcmp((const char*)*attr, "timeShiftBufferDepth") == 0)
      {
        tsbd = (const char*)*(attr + 1);
        dash->has_timeshift_buffer_ = true;
      }
      else if (strcmp((const char*)*attr, "availabilityStartTime") == 0)
        dash->available_time_ = getTime((const char*)*(attr + 1));
      else if (strcmp((const char*)*attr, "suggestedPresentationDelay") == 0)
        AddDuration((const char*)*(attr + 1), dash->m_liveDelay, 1);
      else if (strcmp((const char*)*attr, "minimumUpdatePeriod") == 0)
      {
        uint64_t dur(0);
        AddDuration((const char*)*(attr + 1), dur, 1500);
        // 0S minimumUpdatePeriod = refresh after every segment
        // We already do that so lets set our minimum updateInterval to 30s
        if (dur == 0)
          dur = 30000;
        dash->SetUpdateInterval(static_cast<uint32_t>(dur));
      }
      attr += 2;
    }

    if (!mpt)
      mpt = tsbd;

    AddDuration(mpt, dash->overallSeconds_, 1);
    dash->has_overall_seconds_ = dash->overallSeconds_ > 0;

    uint64_t overallsecs(dash->overallSeconds_ ? dash->overallSeconds_ + 60 : 86400);
    dash->minPresentationOffset = ~0ULL;

    dash->currentNode_ |= MPDNODE_MPD;
  }
}

/*----------------------------------------------------------------------
|   expat text
+---------------------------------------------------------------------*/
static void XMLCALL text(void* data, const char* s, int len)
{
  DASHTree* dash(reinterpret_cast<DASHTree*>(data));
  if (dash->currentNode_ &
      (MPDNODE_BASEURL | MPDNODE_PSSH | MPDNODE_PLAYREADYWRMHEADER | MPDNODE_LOCATION))
    dash->strXMLText_ += std::string(s, len);
}

/*----------------------------------------------------------------------
|   expat end
+---------------------------------------------------------------------*/

static void XMLCALL end(void* data, const char* el)
{
  DASHTree* dash(reinterpret_cast<DASHTree*>(data));

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
              //! @TODO: Multi BaseURL tag is not supported/implemented,
              //! currently we pick wrongly always the LAST BaseURL available.
              //! 
              //! Implementing this feature is very hard without a full refactor
              //! of the parser and classes where the parserization of the XML
              //! data must be decoupled from data processing.
              //! 
              //! There are two cases:
              //! 1) BaseURL without properties
              //!  <BaseURL>https://cdnurl1/</BaseURL>
              //!  the player must select the first base url by default and fallback
              //!  to the others when an address no longer available or not reachable.
              //! 2) BaseURL with DVB properties (ETSI TS 103 285 - DVB)
              //!  <BaseURL dvb:priority="1" dvb:weight="10" serviceLocation="A" >https://cdnurl1/</BaseURL>
              //!  where these properties affect the behaviour of the url selection.

              while (dash->strXMLText_.size() &&
                     (dash->strXMLText_[0] == '\n' || dash->strXMLText_[0] == '\r'))
                dash->strXMLText_.erase(dash->strXMLText_.begin());

              std::string url{dash->strXMLText_};
              if (URL::IsUrlRelative(url))
                url = URL::Join(dash->current_adaptationset_->base_url_, url);

              dash->current_representation_->base_url_ = url;

              if (dash->current_representation_->flags_ & AdaptiveTree::Representation::TEMPLATE)
              {
                if (dash->current_representation_->flags_ &
                    AdaptiveTree::Representation::INITIALIZATION)
                {
                  dash->current_representation_->url_ =
                      URL::Join(url, dash->current_representation_->segtpl_.initialization.substr(
                                         dash->current_adaptationset_->base_url_.size()));
                }
                dash->current_representation_->segtpl_.media_url =
                    URL::Join(url, dash->current_representation_->segtpl_.media.substr(
                                        dash->current_adaptationset_->base_url_.size()));

                ReplacePlaceHolders(dash->current_representation_->segtpl_.media_url,
                                    dash->current_representation_->id,
                                    dash->current_representation_->bandwidth_);
              }
              else
                dash->current_representation_->url_ = url;
              dash->currentNode_ &= ~MPDNODE_BASEURL;
            }
          }
          else if (dash->currentNode_ & MPDNODE_SEGMENTLIST)
          {
            if (strcmp(el, "SegmentList") == 0 || strcmp(el, "SegmentBase") == 0)
            {
              dash->currentNode_ &= ~MPDNODE_SEGMENTLIST;
              if (dash->segcount_ == 0)
                dash->segcount_ = dash->current_representation_->segments_.data.size();
              if (!dash->current_period_->duration_ && dash->current_representation_->timescale_)
              {
                dash->current_period_->timescale_ = dash->current_representation_->timescale_;
                dash->current_period_->duration_ =
                    dash->current_representation_->duration_ *
                    dash->current_representation_->segments_.data.size();
              }
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
              if (StringUtils::EndsWith(el, "pssh"))
              {
                dash->current_pssh_ = dash->strXMLText_;
                dash->currentNode_ &= ~MPDNODE_PSSH;
              }
            }
            else if (strcmp(el, "ContentProtection") == 0)
            {
              if (dash->current_pssh_.empty())
                dash->current_pssh_ = "FILE";
              dash->current_representation_->pssh_set_ =
                  static_cast<uint8_t>(dash->insert_psshset(dash->current_adaptationset_->type_));
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
                dash->current_representation_->pssh_set_ =
                    static_cast<uint8_t>(dash->insert_psshset(dash->current_adaptationset_->type_));
                dash->current_period_->encryptionState_ |= DASHTree::ENCRYTIONSTATE_SUPPORTED;
              }
              else
              {
                delete dash->current_representation_;
                dash->current_adaptationset_->representations_.pop_back();
                return;
              }
            }

            if (dash->current_representation_->segments_.data.empty())
            {
              DASHTree::SegmentTemplate& tpl(dash->current_representation_->segtpl_);

              uint64_t overallSeconds =
                  dash->current_period_->duration_
                      ? dash->current_period_->duration_ / dash->current_period_->timescale_
                      : dash->overallSeconds_;
              if (!tpl.media.empty() && overallSeconds > 0 && tpl.timescale > 0 &&
                  (tpl.duration > 0 ||
                   dash->current_adaptationset_->segment_durations_.data.size()))
              {
                size_t countSegs{dash->current_adaptationset_->segment_durations_.data.size()};
                if (countSegs == 0)
                {
                  countSegs =
                      static_cast<size_t>(static_cast<double>(overallSeconds) /
                                          (static_cast<double>(tpl.duration) / tpl.timescale)) +
                      1;
                }

                if (countSegs < 65536)
                {
                  DASHTree::Segment seg;

                  dash->current_representation_->flags_ |= DASHTree::Representation::TEMPLATE;

                  dash->current_representation_->segments_.data.reserve(countSegs);
                  if (!tpl.initialization.empty())
                  {
                    seg.range_end_ = ~0ULL;
                    dash->current_representation_->initialization_ = seg;
                    dash->current_representation_->flags_ |=
                        DASHTree::Representation::INITIALIZATION;
                  }

                  std::vector<uint32_t>::const_iterator sdb(
                      dash->current_adaptationset_->segment_durations_.data.begin()),
                      sde(dash->current_adaptationset_->segment_durations_.data.end());
                  bool timeBased = sdb != sde && tpl.media.find("$Time") != std::string::npos;
                  if (dash->adp_timelined_)
                    dash->current_representation_->flags_ |= AdaptiveTree::Representation::TIMELINE;

                  seg.range_end_ = dash->current_representation_->startNumber_;
                  if (dash->current_adaptationset_->startPTS_ >
                      dash->base_time_ * dash->current_adaptationset_->timescale_)
                    seg.startPTS_ = dash->current_adaptationset_->startPTS_ -
                                    dash->base_time_ * dash->current_adaptationset_->timescale_;
                  else
                    seg.startPTS_ = 0;
                  seg.range_begin_ = dash->current_adaptationset_->startPTS_;

                  if (!timeBased && dash->has_timeshift_buffer_ &&
                      tpl.duration)
                  {
                    uint64_t sample_time = dash->current_period_->start_ /  1000;

                    seg.range_end_ += (static_cast<int64_t>(dash->stream_start_ - dash->available_time_ -
                                                            overallSeconds - sample_time)) *
                                          tpl.timescale / tpl.duration +
                                      1;
                  }
                  else if (!tpl.duration)
                    tpl.duration = static_cast<unsigned int>(
                        (overallSeconds * tpl.timescale) /
                        dash->current_adaptationset_->segment_durations_.data.size());

                  for (; countSegs; --countSegs)
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
              else if (!(dash->current_representation_->flags_ &
                         (DASHTree::Representation::SEGMENTBASE |
                          DASHTree::Representation::SUBTITLESTREAM)))
              {
                //Let us try to extract the fragments out of SIDX atom
                dash->current_representation_->flags_ |= DASHTree::Representation::SEGMENTBASE;
                dash->current_representation_->indexRangeMin_ = 0;
                dash->current_representation_->indexRangeMax_ = 1024 * 200;
              }
            }
            else
            {
              ReplacePlaceHolders(dash->current_representation_->url_,
                                  dash->current_representation_->id,
                                  dash->current_representation_->bandwidth_);
              dash->current_representation_->segtpl_.media_url =
                  dash->current_representation_->segtpl_.media;
              ReplacePlaceHolders(dash->current_representation_->segtpl_.media_url,
                                  dash->current_representation_->id,
                                  dash->current_representation_->bandwidth_);
            }

            if ((dash->current_representation_->flags_ & DASHTree::Representation::SEGMENTBASE) &&
              dash->current_representation_->indexRangeMin_ == 0 && dash->current_representation_->indexRangeMax_ > 0
              && !(dash->current_representation_->flags_ & DASHTree::Representation::INITIALIZATION))
            {
              // AdaptiveStream::ParseIndexRange will fix the initialization max to its real value.
              dash->current_representation_->flags_ |= DASHTree::Representation::INITIALIZATION;
              dash->current_representation_->initialization_.range_begin_ = 0;
              dash->current_representation_->initialization_.range_end_ =
                  dash->current_representation_->indexRangeMax_;
            }


            if ((dash->current_representation_->flags_ &
                 AdaptiveTree::Representation::INITIALIZATION) == 0 &&
                !dash->current_representation_->segments_.empty())
              // we assume that we have a MOOV atom included in each segment (max 100k = youtube)
              dash->current_representation_->flags_ |=
                  AdaptiveTree::Representation::INITIALIZATION_PREFIXED;

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
            URL::EnsureEndingBackslash(dash->strXMLText_);

            if (URL::IsUrlAbsolute(dash->strXMLText_))
                dash->current_adaptationset_->base_url_ = dash->strXMLText_;
            else
              dash->current_adaptationset_->base_url_ =
                  URL::Join(dash->current_period_->base_url_, dash->strXMLText_);
            dash->currentNode_ &= ~MPDNODE_BASEURL;
          }
        }
        else if (dash->currentNode_ & (MPDNODE_SEGMENTLIST | MPDNODE_SEGMENTTEMPLATE))
        {
          if (dash->currentNode_ & MPDNODE_SEGMENTTIMELINE)
          {
            if (strcmp(el, "SegmentTimeline") == 0)
            {
              if (!dash->current_period_->duration_ &&
                  dash->current_adaptationset_->segtpl_.timescale)
              {
                dash->current_period_->timescale_ = dash->current_adaptationset_->segtpl_.timescale;
                uint64_t sum(0);
                for (auto dur : dash->current_adaptationset_->segment_durations_.data)
                  sum += dur;
                dash->current_period_->duration_ = sum;
              }
              dash->currentNode_ &= ~MPDNODE_SEGMENTTIMELINE;
            }
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
            if (StringUtils::EndsWith(el, "pssh"))
            {
              dash->current_pssh_ = dash->strXMLText_;
              dash->currentNode_ &= ~MPDNODE_PSSH;
            }
          }
          else if (strcmp(el, "ContentProtection") == 0)
          {
            if (dash->current_pssh_.empty())
              dash->current_pssh_ = "FILE";
            dash->adp_pssh_set_ =
                static_cast<uint8_t>(dash->insert_psshset(dash->current_adaptationset_->type_));
            dash->currentNode_ &= ~MPDNODE_CONTENTPROTECTION;
          }
        }
        else if (dash->currentNode_ & MPDNODE_PLAYREADYWRMHEADER)
        {
          dash->current_playready_wrmheader_ = dash->strXMLText_;
          dash->currentNode_ &= ~MPDNODE_PLAYREADYWRMHEADER;
        }
        else if (strcmp(el, "AdaptationSet") == 0)
        {
          dash->currentNode_ &= ~MPDNODE_ADAPTIONSET;
          if (dash->current_adaptationset_->type_ == DASHTree::NOTYPE ||
              (dash->adp_pssh_set_ == 0xFF && dash->current_hasRepURN_) ||
              dash->current_adaptationset_->representations_.empty())
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
                if (dash->current_defaultKID_.empty() &&
                    !dash->current_playready_wrmheader_.empty())
                  dash->current_defaultKID_ =
                      PRProtectionParser(dash->current_playready_wrmheader_).getKID();
                dash->adp_pssh_set_ =
                    static_cast<uint8_t>(dash->insert_psshset(dash->current_adaptationset_->type_));
                dash->current_period_->encryptionState_ |= DASHTree::ENCRYTIONSTATE_SUPPORTED;
              }

              for (std::vector<DASHTree::Representation*>::iterator
                       b(dash->current_adaptationset_->representations_.begin()),
                   e(dash->current_adaptationset_->representations_.end());
                   b != e; ++b)
                if (!(*b)->pssh_set_)
                  (*b)->pssh_set_ = dash->adp_pssh_set_;
            }

            if (dash->current_adaptationset_->segment_durations_.data.empty() &&
                !dash->current_adaptationset_->segtpl_.media.empty())
            {
              for (std::vector<DASHTree::Representation*>::iterator
                       b(dash->current_adaptationset_->representations_.begin()),
                   e(dash->current_adaptationset_->representations_.end());
                   b != e; ++b)
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
                       b(dash->current_adaptationset_->representations_.begin()),
                   e(dash->current_adaptationset_->representations_.end());
                   b != e; ++b)
              {
                if ((*b)->flags_ & DASHTree::Representation::TIMELINE)
                  continue;
                std::vector<uint32_t>::const_iterator sdb(
                    dash->current_adaptationset_->segment_durations_.data.begin()),
                    sde(dash->current_adaptationset_->segment_durations_.data.end());
                uint64_t spts(0);
                for (std::vector<DASHTree::Segment>::iterator sb((*b)->segments_.data.begin()),
                     se((*b)->segments_.data.end());
                     sb != se && sdb != sde; ++sb, ++sdb)
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
          while (dash->strXMLText_.size() &&
                 (dash->strXMLText_[0] == '\n' || dash->strXMLText_[0] == '\r'))
          {
            dash->strXMLText_.erase(dash->strXMLText_.begin());
          }

          URL::EnsureEndingBackslash(dash->strXMLText_);

          if (URL::IsUrlAbsolute(dash->strXMLText_))
          {
            dash->current_period_->base_url_ = dash->strXMLText_;
          }
          else
          {
            dash->current_period_->base_url_ =
                URL::Join(dash->current_period_->base_url_, dash->strXMLText_);
          }
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
        while (dash->strXMLText_.size() &&
               (dash->strXMLText_[0] == '\n' || dash->strXMLText_[0] == '\r'))
        {
          dash->strXMLText_.erase(dash->strXMLText_.begin());
        }

        URL::EnsureEndingBackslash(dash->strXMLText_);

        if (URL::IsUrlAbsolute(dash->strXMLText_))
        {
          dash->mpd_url_ = dash->strXMLText_;
        }
        else
        {
          dash->mpd_url_ = URL::Join(dash->mpd_url_, dash->strXMLText_);
        }
        dash->currentNode_ &= ~MPDNODE_BASEURL;
      }
    }
    else if (dash->currentNode_ & MPDNODE_LOCATION)
    {
      if (strcmp(el, "Location") == 0)
      {
        while (dash->strXMLText_.size() &&
               (dash->strXMLText_[0] == '\n' || dash->strXMLText_[0] == '\r'))
          dash->strXMLText_.erase(dash->strXMLText_.begin());
        if (dash->strXMLText_.compare(0, 7, "http://") == 0 ||
            dash->strXMLText_.compare(0, 8, "https://") == 0)
          dash->location_ = dash->strXMLText_;
        dash->currentNode_ &= ~MPDNODE_LOCATION;
      }
    }
    else if (strcmp(el, "MPD") == 0)
    {
      //cleanup periods
      for (std::vector<AdaptiveTree::Period*>::iterator b(dash->periods_.begin());
           b != dash->periods_.end();)
      {
        if (dash->has_overall_seconds_ && !(*b)->duration_)
        {
          if (b + 1 == dash->periods_.end())
            (*b)->duration_ =
                ((dash->overallSeconds_ * 1000 - (*b)->start_) * (*b)->timescale_) / 1000;
          else
            (*b)->duration_ = (((*(b + 1))->start_ - (*b)->start_) * (*b)->timescale_) / 1000;
        }
        if ((*b)->adaptationSets_.empty())
        {
          if (dash->has_overall_seconds_)
            dash->overallSeconds_ -= (*b)->duration_ / (*b)->timescale_;
          delete *b;
          b = dash->periods_.erase(b);
        }
        else
        {
          if (!dash->has_overall_seconds_)
            dash->overallSeconds_ += (*b)->duration_ / (*b)->timescale_;
          ++b;
        }
      }
      dash->currentNode_ &= ~MPDNODE_MPD;
    }
  }
}

DASHTree::DASHTree(const DASHTree& left) : AdaptiveTree(left)
{
  base_time_ = left.base_time_;
  
  // Location element should be used on manifest updates
  location_ = left.location_;
}

/*----------------------------------------------------------------------
|   DASHTree
+---------------------------------------------------------------------*/
bool DASHTree::open(const std::string& url)
{
  return open(url, {});
}

bool DASHTree::open(const std::string& url, std::map<std::string, std::string> addHeaders, bool isUpdate)
{
  currentNode_ = 0;

  std::stringstream data;
  HTTPRespHeaders respHeaders;
  if (!DownloadManifest(url, addHeaders, data, respHeaders))
    return false;

  SaveManifest("", data, url);

  effective_url_ = respHeaders.m_effectiveUrl;
  m_manifestRespHeaders = respHeaders;

  if (!PreparePaths(effective_url_))
    return false;

  if (!ParseManifest(data.str()))
    return false;

  if (periods_.empty())
  {
    LOG::Log(LOGWARNING, "No periods in the manifest");
    return false;
  }

  current_period_ = periods_[0];
  SortTree();
  if (!isUpdate)
    StartUpdateThread();

  return true;
}

void DASHTree::SetManifestUpdateParam(std::string& manifestUrl, std::string_view param)
{
  m_manifestUpdateParam = param;
  if (m_manifestUpdateParam.empty())
  {
    m_manifestUpdateParam = URL::GetParametersFromPlaceholder(manifestUrl, "$START_NUMBER$");
    manifestUrl.resize(manifestUrl.size() - m_manifestUpdateParam.size());
  }
}

bool DASHTree::ParseManifest(const std::string& data)
{
  strXMLText_.clear();

  parser_ = XML_ParserCreate(nullptr);
  if (!parser_)
    return false;

  XML_SetUserData(parser_, (void*)this);
  XML_SetElementHandler(parser_, start, end);
  XML_SetCharacterDataHandler(parser_, text);
  int isDone{0};
  XML_Status status{XML_Parse(parser_, data.c_str(), static_cast<int>(data.size()), isDone)};

  XML_ParserFree(parser_);
  parser_ = nullptr;

  if (status == XML_STATUS_ERROR)
  {
    LOG::LogF(LOGERROR, "Failed to parse the manifest file");
    return false;
  }

  return true;
}

//Called each time before we switch to a new segment
void DASHTree::RefreshSegments(Period* period,
                               AdaptationSet* adp,
                               Representation* rep,
                               StreamType type)
{
  if ((type == VIDEO || type == AUDIO))
  {
    m_updThread.ResetStartTime();
    RefreshLiveSegments();
  }
}

//Can be called form update-thread!
//! @todo: we are updating variables in non-thread safe way
void DASHTree::RefreshLiveSegments()
{
  lastUpdated_ = std::chrono::system_clock::now();

  if (m_manifestUpdateParam.empty())
  {
    LOG::LogF(LOGERROR, "Cannot refresh live segments, manifest update param is not set");
    return;
  }

  size_t numReplace{~(size_t)0};
  size_t nextStartNumber{~(size_t)0};

  std::unique_ptr<DASHTree> updateTree{std::move(Clone())};

  std::string manifestUrlUpd{manifest_url_};
  bool urlHaveStartNumber{m_manifestUpdateParam.find("$START_NUMBER$") != std::string::npos};

  if (urlHaveStartNumber)
  {
    for (auto period : periods_)
    {
      if (!period)
        continue;
      for (auto adaptSet : period->adaptationSets_)
      {
        if (!adaptSet)
          continue;
        for (auto repr : adaptSet->representations_)
        {
          if (!repr)
            continue;

          if (repr->startNumber_ + repr->segments_.size() < nextStartNumber)
            nextStartNumber = repr->startNumber_ + repr->segments_.size();

          size_t replaceable = repr->getCurrentSegmentPos() + 1;
          if (replaceable == 0) // (~0ULL + 1) == 0
            replaceable = repr->segments_.size();

          if (replaceable < numReplace)
            numReplace = replaceable;
        }
      }
    }
    LOG::LogF(LOGDEBUG, "Manifest URL start number param set to: %zu (numReplace: %zu)",
              nextStartNumber, numReplace);

    // Add the manifest update url parameter with the predefined url parameters
    std::string updateParam = m_manifestUpdateParam;

    STRING::ReplaceFirst(updateParam, "$START_NUMBER$", std::to_string(nextStartNumber));

    URL::AppendParameters(updateTree->m_manifestParams, updateParam);
  }

  std::map<std::string, std::string> addHeaders;

  if (!urlHaveStartNumber)
  {
    if (!m_manifestRespHeaders.m_etag.empty())
      addHeaders["If-None-Match"] = "\"" + m_manifestRespHeaders.m_etag + "\"";

    if (!m_manifestRespHeaders.m_lastModified.empty())
      addHeaders["If-Modified-Since"] = m_manifestRespHeaders.m_lastModified;
  }
  //m_manifestParams
  if (updateTree->open(manifestUrlUpd, addHeaders, true))
  {
    m_manifestRespHeaders = updateTree->m_manifestRespHeaders;
    location_ = updateTree->location_;

    //Youtube returns last smallest number in case the requested data is not available
    if (urlHaveStartNumber && updateTree->firstStartNumber_ < nextStartNumber)
      return;

    for (size_t index{0}; index < updateTree->periods_.size(); index++)
    {
      auto updPeriod = updateTree->periods_[index];
      if (!updPeriod)
        continue;

      Period* period{nullptr};
      // find matching period based on ID
      auto itPd = std::find_if(periods_.begin(), periods_.end(),
                               [&updPeriod](const Period* item)
                               { return !item->id_.empty() && item->id_ == updPeriod->id_; });
      if (itPd == periods_.end())
      {
        // not found, try matching period based on start
        itPd = std::find_if(periods_.begin(), periods_.end(),
                            [&updPeriod](const Period* item)
                            { return item->start_ && item->start_ == updPeriod->start_; });
      }
      // found!
      if (itPd != periods_.end())
        period = *itPd;
      if (!period && updPeriod->id_.empty() && updPeriod->start_ == 0)
      {
        // not found, fallback match based on position
        if (index < periods_.size())
          period = periods_[index];
      }
      // new period, insert it
      if (!period)
      {
        LOG::LogF(LOGDEBUG, "Inserting new Period (id=%s, start=%ld)", updPeriod->id_.c_str(),
                  updPeriod->start_);
        updateTree->periods_[index] = nullptr; // remove to prevent delete; we take ownership
        updPeriod->sequence_ = current_sequence_++;
        periods_.push_back(updPeriod);
        continue;
      }

      for (auto updAdaptationSet : updPeriod->adaptationSets_)
      {
        // Locate adaptationset
        if (!updAdaptationSet)
          continue;

        for (auto adaptationSet : period->adaptationSets_)
        {
          if (!(adaptationSet->id_ == updAdaptationSet->id_ &&
                adaptationSet->group_ == updAdaptationSet->group_ &&
                adaptationSet->type_ == updAdaptationSet->type_ &&
                adaptationSet->mimeType_ == updAdaptationSet->mimeType_ &&
                adaptationSet->language_ == updAdaptationSet->language_))
            continue;

          for (auto updRepr : updAdaptationSet->representations_)
          {
            // Locate representation
            auto itR = std::find_if(
                adaptationSet->representations_.begin(), adaptationSet->representations_.end(),
                [&updRepr](const Representation* item) { return item->id == updRepr->id; });

            if (!updRepr->segments_.Get(0))
            {
              LOG::LogF(LOGERROR,
                        "Segment at position 0 not found from (update) representation id: %s",
                        updRepr->id.c_str());
              return;
            }

            if (itR != adaptationSet->representations_.end()) // Found representation
            {
              auto repr = *itR;

              if (!repr->segments_.empty())
              {
                if (urlHaveStartNumber) // Partitial update
                {
                  // Insert new segments
                  uint64_t ptsOffset = repr->nextPts_ - updRepr->segments_.Get(0)->startPTS_;
                  size_t currentPos = repr->getCurrentSegmentPos();
                  size_t repFreeSegments{numReplace};

                  auto updSegmentIt(updRepr->segments_.data.begin());
                  for (; updSegmentIt != updRepr->segments_.data.end() && repFreeSegments != 0;
                       updSegmentIt++)
                  {
                    LOG::LogF(LOGDEBUG, "Insert representation (id: %s url: %s)",
                              updRepr->id.c_str(), updSegmentIt->url.c_str());
                    if (repr->flags_ & Representation::URLSEGMENTS)
                    {
                      Segment* segment{repr->segments_.Get(0)};
                      if (!segment)
                      {
                        LOG::LogF(LOGERROR,
                                  "Segment at position 0 not found from representation id: %s",
                                  repr->id.c_str());
                        return;
                      }
                      segment->url.clear();
                    }
                    updSegmentIt->startPTS_ += ptsOffset;
                    repr->segments_.insert(*updSegmentIt);
                    if (repr->flags_ & Representation::URLSEGMENTS)
                      updSegmentIt->url.clear();
                    repr->startNumber_++;
                    repFreeSegments--;
                  }

                  //We have renewed the current segment
                  if (!repFreeSegments && numReplace == currentPos + 1)
                    repr->current_segment_ = nullptr;

                  if ((repr->flags_ & Representation::WAITFORSEGMENT) &&
                      repr->get_next_segment(repr->current_segment_))
                  {
                    repr->flags_ &= ~Representation::WAITFORSEGMENT;
                    LOG::LogF(LOGDEBUG, "End WaitForSegment stream %s", repr->id.c_str());
                  }

                  if (updSegmentIt == updRepr->segments_.data.end())
                    repr->nextPts_ += updRepr->nextPts_;
                  else
                    repr->nextPts_ += updSegmentIt->startPTS_;
                }
                else if (updRepr->startNumber_ <= 1) // Full update, be careful with startnumbers!
                {
                  //TODO: check if first element or size differs
                  uint64_t segmentId(repr->getCurrentSegmentNumber());
                  if (repr->flags_ & DASHTree::Representation::TIMELINE)
                  {
                    uint64_t search_pts = updRepr->segments_.Get(0)->range_begin_;
                    uint64_t misaligned = 0;
                    for (const auto& segment : repr->segments_.data)
                    {
                      if (misaligned)
                      {
                        uint64_t ptsDiff = segment.range_begin_ - (&segment - 1)->range_begin_;
                        // our misalignment is small ( < 2%), let's decrement the start number
                        if (misaligned < (ptsDiff * 2 / 100))
                          --repr->startNumber_;
                        break;
                      }
                      if (segment.range_begin_ == search_pts)
                        break;
                      else if (segment.range_begin_ > search_pts)
                      {
                        if (&repr->segments_.data.front() == &segment)
                        {
                          --repr->startNumber_;
                          break;
                        }
                        misaligned = search_pts - (&segment - 1)->range_begin_;
                      }
                      else
                        ++repr->startNumber_;
                    }
                  }
                  else if (updRepr->segments_.Get(0)->startPTS_ ==
                           repr->segments_.Get(0)->startPTS_)
                  {
                    uint64_t search_re = updRepr->segments_.Get(0)->range_end_;
                    for (const auto& segment : repr->segments_.data)
                    {
                      if (segment.range_end_ >= search_re)
                        break;
                      ++repr->startNumber_;
                    }
                  }
                  else
                  {
                    uint64_t search_pts = updRepr->segments_.Get(0)->startPTS_;
                    for (const auto& segment : repr->segments_.data)
                    {
                      if (segment.startPTS_ >= search_pts)
                        break;
                      ++repr->startNumber_;
                    }
                  }

                  updRepr->segments_.swap(repr->segments_);
                  if (!~segmentId || segmentId < repr->startNumber_)
                    repr->current_segment_ = nullptr;
                  else
                  {
                    if (segmentId >= repr->startNumber_ + repr->segments_.size())
                      segmentId = repr->startNumber_ + repr->segments_.size() - 1;
                    repr->current_segment_ =
                        repr->get_segment(static_cast<size_t>(segmentId - repr->startNumber_));
                  }

                  if ((repr->flags_ & Representation::WAITFORSEGMENT) &&
                      repr->get_next_segment(repr->current_segment_))
                    repr->flags_ &= ~Representation::WAITFORSEGMENT;

                  LOG::LogF(LOGDEBUG,
                            "Full update without start number (repr. id: %s current start: %u)",
                            updRepr->id.c_str(), repr->startNumber_);
                  overallSeconds_ = updateTree->overallSeconds_;
                }
                else if (updRepr->startNumber_ > repr->startNumber_ ||
                         (updRepr->startNumber_ == repr->startNumber_ &&
                          updRepr->segments_.size() > repr->segments_.size()))
                {
                  size_t segmentId(repr->getCurrentSegmentNumber());
                  updRepr->segments_.swap(repr->segments_);
                  repr->startNumber_ = updRepr->startNumber_;
                  if (!~segmentId || segmentId < repr->startNumber_)
                    repr->current_segment_ = nullptr;
                  else
                  {
                    if (segmentId >= repr->startNumber_ + repr->segments_.size())
                      segmentId = repr->startNumber_ + repr->segments_.size() - 1;
                    repr->current_segment_ =
                        repr->get_segment(static_cast<size_t>(segmentId - repr->startNumber_));
                  }

                  if ((repr->flags_ & Representation::WAITFORSEGMENT) &&
                      repr->get_next_segment(repr->current_segment_))
                  {
                    repr->flags_ &= ~Representation::WAITFORSEGMENT;
                  }
                  LOG::LogF(LOGDEBUG,
                            "Full update with start number (repr. id: %s current start:%u)",
                            updRepr->id.c_str(), repr->startNumber_);
                  overallSeconds_ = updateTree->overallSeconds_;
                }
              }
            }
          }
        }
      }
    }
  }
}
