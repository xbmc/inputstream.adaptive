/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DASHTree.h"

#include "CompKodiProps.h"
#include "PRProtectionParser.h"
#include "SrvBroker.h"
#include "common/Period.h"
#include "decrypters/Helpers.h"
#include "utils/Base64Utils.h"
#include "utils/CurlUtils.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/Utils.h"
#include "utils/XMLUtils.h"
#include "utils/log.h"

#include <algorithm> // max
#include <cmath>
#include <cstdio> // sscanf
#include <numeric> // accumulate
#include <string>
#include <thread>

#include <pugixml.hpp>

using namespace pugi;
using namespace kodi::tools;
using namespace PLAYLIST;
using namespace UTILS;

/*
 * Supported dynamic live services:
 * - MPD-controlled live:
 *   - SegmentTemplate with segments, updates are sheduled to call OnUpdateSegments method to retrieve updated segments
 *   - SegmentTemplate without segments, InsertLiveSegment method will be called to add new segments, combined with sheduled updates
 * - Segment-controlled live:
 *   - SegmentTemplate without segments, demuxer parse the packets and calls InsertLiveFragment method to provide new segments
 */

namespace
{
std::string ReplacePlaceHolders(std::string str, const std::string_view id, uint32_t bandwidth)
{
  STRING::ReplaceAll(str, "$RepresentationID$", id);
  STRING::ReplaceAll(str, "$Bandwidth$", std::to_string(bandwidth));
  return str;
}

StreamType DetectStreamType(std::string_view contentType, std::string_view mimeType)
{
  StreamType streamType = StreamType::NOTYPE;

  if (contentType == "video")
    streamType = StreamType::VIDEO;
  else if (contentType == "audio")
    streamType = StreamType::AUDIO;
  else if (contentType == "text")
    streamType = StreamType::SUBTITLE;
  else
  {
    if (STRING::StartsWith(mimeType, "video"))
      streamType = StreamType::VIDEO;
    else if (STRING::StartsWith(mimeType, "audio"))
      streamType = StreamType::AUDIO;
    else if (STRING::StartsWith(mimeType, "application") || STRING::StartsWith(mimeType, "text"))
      streamType = StreamType::SUBTITLE;
  }
  return streamType;
}

PLAYLIST::ContainerType DetectContainerType(std::string_view mimeType)
{
  if (STRING::Contains(mimeType, "/webm"))
    return ContainerType::WEBM;
  if (STRING::Contains(mimeType, "/x-matroska"))
    return ContainerType::MATROSKA;
  if (STRING::Contains(mimeType, "/ttml+xml") || STRING::Contains(mimeType, "vtt"))
    return ContainerType::TEXT;

  return ContainerType::MP4;
}

std::string DetectCodecFromMimeType(std::string_view mimeType)
{
  if (mimeType == "text/vtt")
    return CODEC::FOURCC_WVTT;
  if (mimeType == "application/ttml+xml")
    return CODEC::FOURCC_TTML;

  return "";
}

} // unnamed namespace


adaptive::CDashTree::CDashTree(const CDashTree& left) : AdaptiveTree(left)
{
  m_isCustomInitPssh = left.m_isCustomInitPssh;
}

void adaptive::CDashTree::Configure(CHOOSER::IRepresentationChooser* reprChooser,
                                    std::vector<std::string_view> supportedKeySystems,
                                    std::string_view manifestUpdParams)
{
  AdaptiveTree::Configure(reprChooser, supportedKeySystems, manifestUpdParams);
  m_isCustomInitPssh = !CSrvBroker::GetKodiProps().GetLicenseData().empty();
}

bool adaptive::CDashTree::Open(std::string_view url,
                               const std::map<std::string, std::string>& headers,
                               const std::string& data)
{
  SaveManifest("", data, url);

  m_manifestRespHeaders = headers;
  manifest_url_ = url;
  base_url_ = URL::GetUrlPath(url.data());

  if (!ParseManifest(data))
    return false;

  if (m_periods.empty())
  {
    LOG::Log(LOGWARNING, "No periods in the manifest");
    return false;
  }

  MergeAdpSets();

  auto& kodiProps = CSrvBroker::GetKodiProps();
  //! @todo: can have sense to move period selection on PostInit or another place
  //! just before the session initialize period, that will be a common code for all manifest types,
  //! also because the code related to live delay calculation on AdaptiveStream::start_stream
  //! can potentially fall on the previous period and so its needed to set in advance the right period
  //! so live delay and period selection could be merged
  //! to do this its needed to test also the behaviour of HLS streams with discontinuities
  uint64_t now = stream_start_ - available_time_;
  if (m_isLive && !kodiProps.IsPlayTimeshift())
  {
    for (auto& period : m_periods)
    {
      if (period->GetStart() != NO_VALUE && now >= period->GetStart())
        m_currentPeriod = period.get();
    }

    if (!m_currentPeriod)
      m_currentPeriod = m_periods.back().get();
  }
  else
    m_currentPeriod = m_periods.front().get();

  return true;
}

bool adaptive::CDashTree::ParseManifest(const std::string& data)
{
  xml_document doc;
  xml_parse_result parseRes = doc.load_buffer(data.c_str(), data.size());
  if (parseRes.status != status_ok)
  {
    LOG::LogF(LOGERROR, "Failed to parse the manifest file, error code: %i", parseRes.status);
    return false;
  }

  m_segmentsLowerStartNumber = 0;
  m_periodCurrentSeq = 0;

  xml_node nodeMPD = doc.child("MPD");
  if (!nodeMPD)
  {
    LOG::LogF(LOGERROR, "Failed to get manifest <MPD> tag element.");
    return false;
  }

  // Parse <MPD> tag attributes
  ParseTagMPDAttribs(nodeMPD);

  // Parse <MPD> <Location> tag
  std::string_view locationUrl = nodeMPD.child("Location").child_value();
  if (!locationUrl.empty())
  {
    if (URL::IsUrlRelative(locationUrl))
      location_ = URL::Join(URL::GetBaseDomain(base_url_), locationUrl.data());
    else
      location_ = locationUrl;
  }

  // Parse <MPD> <UTCTiming> tags
  //! @todo: needed implementation
  if (nodeMPD.child("UTCTiming"))
    LOG::LogF(LOGWARNING, "The <UTCTiming> tag element is not supported so playback problems may occur.");

  // Parse <MPD> <BaseURL> tag (just first, multi BaseURL not supported yet)
  std::string mpdUrl = base_url_;
  std::string baseUrl = nodeMPD.child("BaseURL").child_value();
  if (!baseUrl.empty())
  {
    URL::EnsureEndingBackslash(baseUrl);

    if (URL::IsUrlAbsolute(baseUrl))
      mpdUrl = baseUrl;
    else
      mpdUrl = URL::Join(mpdUrl, baseUrl);
  }

  // Parse <MPD> <Period> tags
  for (xml_node node : nodeMPD.children("Period"))
  {
    ParseTagPeriod(node, mpdUrl);
  }

  // For multi-periods streaming must be ensured the duration of each period:
  // - If "duration" attribute is provided on each Period tag, do nothing
  // - If "duration" attribute is missing, but "start" attribute, use this last one to calculate the duration
  // - If both attributes are missing, try get the duration from a representation,
  //   e.g. a single period in a live stream the duration must be determined by the available segments

  uint64_t totalDuration{0}; // Calculated duration, in ms
  uint64_t mpdTotalDuration = m_mediaPresDuration; // MPD total duration, in ms

  {
    for (auto itPeriod = m_periods.begin(); itPeriod != m_periods.end();)
    {
      auto& period = *itPeriod;

      // Skip periods with duration already provided
      if (period->GetDuration() > 0)
      {
        ++itPeriod;
        continue;
      }

      auto nextPeriod = itPeriod + 1;
      if (nextPeriod == m_periods.end()) // Next period, not found
      {
        if (period->GetStart() != NO_VALUE && mpdTotalDuration > 0)
        {
          uint64_t durMs = mpdTotalDuration - period->GetStart();
          period->SetDuration(durMs * period->GetTimescale() / 1000);
        }
        else // Try get duration / timescale from a representation
        {
          CAdaptationSet* adp = CAdaptationSet::FindByFirstAVStream(period->GetAdaptationSets());
          if (adp)
          {
            auto& rep = adp->GetRepresentations()[0];
            if (rep->GetDuration() > 0)
            {
              uint64_t durMs = rep->GetDuration() * 1000 / rep->GetTimescale();
              period->SetDuration(durMs * period->GetTimescale() / 1000);
              totalDuration += durMs;
            }
          }
        }
      }
      else // Next period, found
      {
        if (period->GetStart() != NO_VALUE && (*nextPeriod)->GetStart() != NO_VALUE)
        {
          uint64_t durMs = (*nextPeriod)->GetStart() - period->GetStart();
          period->SetDuration(durMs * period->GetTimescale() / 1000);
        }
        else // Try get duration / timescale from a representation
        {
          CAdaptationSet* adp = CAdaptationSet::FindByFirstAVStream(period->GetAdaptationSets());
          if (adp)
          {
            auto& rep = adp->GetRepresentations()[0];
            if (rep->GetDuration() > 0)
            {
              uint64_t durMs = rep->GetDuration() * 1000 / rep->GetTimescale();
              period->SetDuration(durMs * period->GetTimescale() / 1000);
              totalDuration += durMs;
            }
          }
        }
      }

      ++itPeriod;
    }
  }

  if (mpdTotalDuration == 0)
    mpdTotalDuration = m_timeShiftBufferDepth;

  if (mpdTotalDuration > 0)
    m_totalTime = mpdTotalDuration;
  else
    m_totalTime = totalDuration;

  return true;
}

void adaptive::CDashTree::ParseTagMPDAttribs(pugi::xml_node nodeMPD)
{
  std::string mediaPresDuration;
  if (XML::QueryAttrib(nodeMPD, "mediaPresentationDuration", mediaPresDuration))
    m_mediaPresDuration = static_cast<uint64_t>(XML::ParseDuration(mediaPresDuration) * 1000);

  m_isLive = XML::GetAttrib(nodeMPD, "type") == "dynamic";

  std::string timeShiftBufferDepthStr;
  if (XML::QueryAttrib(nodeMPD, "timeShiftBufferDepth", timeShiftBufferDepthStr))
    m_timeShiftBufferDepth = static_cast<uint64_t>(XML::ParseDuration(timeShiftBufferDepthStr) * 1000);

  std::string availabilityStartTimeStr;
  if (XML::QueryAttrib(nodeMPD, "availabilityStartTime", availabilityStartTimeStr))
    available_time_ = static_cast<uint64_t>(XML::ParseDate(availabilityStartTimeStr) * 1000);

  // If TSB is not set but availabilityStartTime, use the last one as TSB
  // since all segments from availabilityStartTime are available
  if (m_timeShiftBufferDepth == 0 && available_time_ > 0)
    m_timeShiftBufferDepth = stream_start_ - available_time_;

  // TSB can be very large, limit it to avoid excessive memory consumption
  uint64_t tsbLimitMs = 14400000; // Default 4 hours

  auto& manifestCfg = CSrvBroker::GetKodiProps().GetManifestConfig();
  if (manifestCfg.timeShiftBufferLimit.has_value())
    tsbLimitMs = *manifestCfg.timeShiftBufferLimit * 1000;

  if (m_timeShiftBufferDepth > tsbLimitMs)
    m_timeShiftBufferDepth = tsbLimitMs;

  std::string suggestedPresentationDelayStr;
  if (XML::QueryAttrib(nodeMPD, "suggestedPresentationDelay", suggestedPresentationDelayStr))
    m_liveDelay = static_cast<uint64_t>(XML::ParseDuration(suggestedPresentationDelayStr));

  std::string minimumUpdatePeriodStr;
  if (XML::QueryAttrib(nodeMPD, "minimumUpdatePeriod", minimumUpdatePeriodStr))
  {
    double duration = XML::ParseDuration(minimumUpdatePeriodStr);
    m_minimumUpdatePeriod = static_cast<uint64_t>(duration);
    m_updateInterval = static_cast<uint64_t>(duration * 1000);
  }
}

void adaptive::CDashTree::ParseTagPeriod(pugi::xml_node nodePeriod, std::string_view mpdUrl)
{
  std::unique_ptr<CPeriod> period = CPeriod::MakeUniquePtr();

  period->SetSequence(m_periodCurrentSeq++);

  // Parse <Period> attributes

  period->SetId(XML::GetAttrib(nodePeriod, "id"));

  std::string_view start = XML::GetAttrib(nodePeriod, "start");
  if (!start.empty())
    period->SetStart(static_cast<uint64_t>(XML::ParseDuration(start) * 1000));

  period->SetDuration(
      static_cast<uint64_t>(XML::ParseDuration(XML::GetAttrib(nodePeriod, "duration")) * 1000));

  if (period->GetDuration() == 0)
  {
    // If no duration, try look at next Period to determine it
    pugi::xml_node nodeNextPeriod = nodePeriod.next_sibling();
    if (nodeNextPeriod)
    {
      std::string_view nextStartStr = XML::GetAttrib(nodeNextPeriod, "start");
      uint64_t nextStart{0};

      if (!nextStartStr.empty())
        nextStart = static_cast<uint64_t>(XML::ParseDuration(nextStartStr) * 1000);

      if (nextStart > 0)
        period->SetDuration((nextStart - period->GetStart()) * period->GetTimescale() / 1000);
    }
  }

  // Parse <BaseURL> tag (just first, multi BaseURL not supported yet)
  std::string baseUrl = nodePeriod.child("BaseURL").child_value();
  if (baseUrl.empty())
  {
    period->SetBaseUrl(mpdUrl);
  }
  else
  {
    URL::EnsureEndingBackslash(baseUrl);

    if (URL::IsUrlAbsolute(baseUrl))
      period->SetBaseUrl(baseUrl);
    else
      period->SetBaseUrl(URL::Join(mpdUrl.data(), baseUrl));
  }

  // Parse <SegmentTemplate> tag
  xml_node nodeSegTpl = nodePeriod.child("SegmentTemplate");
  if (nodeSegTpl)
  {
    CSegmentTemplate segTemplate;

    ParseSegmentTemplate(nodeSegTpl, segTemplate);

    period->SetSegmentTemplate(segTemplate);
  }

  // Parse <SegmentList> tag
  xml_node nodeSeglist = nodePeriod.child("SegmentList");
  if (nodeSeglist)
  {
    CSegmentList segList;

    uint64_t startNumber;
    if (XML::QueryAttrib(nodeSeglist, "startNumber", startNumber))
      segList.SetStartNumber(startNumber);

    uint64_t duration;
    if (XML::QueryAttrib(nodeSeglist, "duration", duration))
      segList.SetDuration(duration);

    uint32_t timescale;
    if (XML::QueryAttrib(nodeSeglist, "timescale", timescale))
      segList.SetTimescale(timescale);

    period->SetSegmentList(segList);
  }

  // Parse <AdaptationSet> tags
  for (xml_node node : nodePeriod.children("AdaptationSet"))
  {
    ParseTagAdaptationSet(node, period.get());
  }

  m_periods.push_back(std::move(period));
}

void adaptive::CDashTree::ParseTagAdaptationSet(pugi::xml_node nodeAdp, PLAYLIST::CPeriod* period)
{
  std::unique_ptr<CAdaptationSet> adpSet = CAdaptationSet::MakeUniquePtr(period);

  adpSet->SegmentTimelineDuration() = period->SegmentTimelineDuration();

  std::string id;
  // "audioTrackId" tag is amazon VOD specific, since dont use the standard "id" tag
  // this to to make MergeAdpSets more effective for some limit case
  if (XML::QueryAttrib(nodeAdp, "id", id) || XML::QueryAttrib(nodeAdp, "audioTrackId", id))
    adpSet->SetId(id);

  std::string contentType;

  // Parse <ContentComponent> child tag
  xml_node nodeContComp = nodeAdp.child("ContentComponent");
  if (nodeContComp)
  {
    if (adpSet->GetId().empty())
      adpSet->SetId(XML::GetAttrib(nodeContComp, "id"));

    contentType = XML::GetAttrib(nodeContComp, "contentType");
  }

  // Parse <Role> child tag
  xml_node nodeRole = nodeAdp.child("Role");
  if (nodeRole)
  {
    std::string_view schemeIdUri = XML::GetAttrib(nodeRole, "schemeIdUri");
    std::string_view value = XML::GetAttrib(nodeRole, "value");

    if (schemeIdUri == "urn:mpeg:dash:role:2011")
    {
      if (value == "subtitle")
        contentType = "text";
      else if (value == "forced") // ISA custom attribute
        adpSet->SetIsForced(true);
      else if (value == "main")
        adpSet->SetIsDefault(true);
      else if (value == "caption" || value == "alternate" || value == "commentary")
        adpSet->SetIsImpaired(true);
    }
  }

  // Parse <Accessibility> child tag
  xml_node nodeAcc = nodeAdp.child("Accessibility");
  if (nodeAcc)
  {
    std::string_view schemeIdUri = XML::GetAttrib(nodeAcc, "schemeIdUri");
    std::string_view value = XML::GetAttrib(nodeAcc, "value");

    if (schemeIdUri == "urn:mpeg:dash:role:2011")
    {
      if (STRING::StartsWith(value, "caption")) // caption or captions
        adpSet->SetIsImpaired(true);
    }
  }

  if (contentType.empty())
    contentType = XML::GetAttrib(nodeAdp, "contentType");

  adpSet->SetMimeType(XML::GetAttrib(nodeAdp, "mimeType"));

  adpSet->SetStreamType(DetectStreamType(contentType, adpSet->GetMimeType()));
  adpSet->SetContainerType(DetectContainerType(adpSet->GetMimeType()));

  if (adpSet->GetContainerType() == ContainerType::NOTYPE)
  {
    LOG::LogF(LOGWARNING, "Skipped AdaptationSet with id: \"%s\", container type not specified.",
              adpSet->GetId().data());
    return;
  }

  adpSet->SetGroup(XML::GetAttrib(nodeAdp, "group"));
  adpSet->SetLanguage(XML::GetAttrib(nodeAdp, "lang"));
  adpSet->SetName(XML::GetAttrib(nodeAdp, "name"));
  adpSet->SetResWidth(XML::GetAttribInt(nodeAdp, "width"));
  adpSet->SetResHeight(XML::GetAttribInt(nodeAdp, "height"));

  uint32_t frameRate{0};
  uint32_t frameRateScale{1}; // Default 1 when attribute value has framerate without scale
  std::sscanf(XML::GetAttrib(nodeAdp, "frameRate").data(), "%" SCNu32 "/%" SCNu32, &frameRate,
              &frameRateScale);
  adpSet->SetFrameRate(frameRate);
  adpSet->SetFrameRateScale(frameRateScale);

  int parW{0};
  int parH{0};
  if (std::sscanf(XML::GetAttrib(nodeAdp, "par").data(), "%d:%d", &parW, &parH) == 2)
    adpSet->SetAspectRatio(static_cast<float>(parW) / parH);

  adpSet->AddCodecs(XML::GetAttrib(nodeAdp, "codecs"));

  // Following stream properties, can be used to override existing values
  std::string isImpaired;
  if (XML::QueryAttrib(nodeAdp, "impaired", isImpaired)) // ISA custom attribute
    adpSet->SetIsImpaired(isImpaired == "true");

  std::string isForced;
  if (XML::QueryAttrib(nodeAdp, "forced", isForced)) // ISA custom attribute
    adpSet->SetIsForced(isForced == "true");

  std::string isOriginal;
  if (XML::QueryAttrib(nodeAdp, "original", isOriginal)) // ISA custom attribute
    adpSet->SetIsOriginal(isOriginal == "true");

  std::string isDefault;
  if (XML::QueryAttrib(nodeAdp, "default", isDefault)) // ISA custom attribute
    adpSet->SetIsDefault(isDefault == "true");

  // Parse <AudioChannelConfiguration> child tag
  xml_node nodeAudioCh = nodeAdp.child("AudioChannelConfiguration");
  if (nodeAudioCh)
    adpSet->SetAudioChannels(ParseAudioChannelConfig(nodeAudioCh));

  // Parse <SupplementalProperty> child tags
  for (xml_node nodeSP : nodeAdp.children("SupplementalProperty"))
  {
    std::string_view schemeIdUri = XML::GetAttrib(nodeSP, "schemeIdUri");
    std::string_view value = XML::GetAttrib(nodeSP, "value");

    if (schemeIdUri == "urn:mpeg:dash:adaptation-set-switching:2016")
      adpSet->AddSwitchingIds(value);
    else if (schemeIdUri == "http://dashif.org/guidelines/last-segment-number")
      adpSet->SetSegmentEndNr(STRING::ToUint64(value));
  }

  // Parse <BaseURL> tag (just first, multi BaseURL not supported yet)
  std::string baseUrlText = nodeAdp.child("BaseURL").child_value();
  if (baseUrlText.empty())
  {
    adpSet->SetBaseUrl(period->GetBaseUrl());
  }
  else
  {
    URL::EnsureEndingBackslash(baseUrlText);

    if (URL::IsUrlAbsolute(baseUrlText))
      adpSet->SetBaseUrl(baseUrlText);
    else
      adpSet->SetBaseUrl(URL::Join(period->GetBaseUrl(), baseUrlText));
  }

  // Parse <SegmentTemplate> tag
  xml_node nodeSegTpl = nodeAdp.child("SegmentTemplate");
  if (nodeSegTpl || period->HasSegmentTemplate())
  {
    CSegmentTemplate segTemplate{period->GetSegmentTemplate()};

    if (nodeSegTpl)
      ParseSegmentTemplate(nodeSegTpl, segTemplate);

    adpSet->SetSegmentTemplate(segTemplate);
  }

  // Parse <SegmentList> tag
  xml_node nodeSeglist = nodeAdp.child("SegmentList");
  if (nodeSeglist)
  {
    CSegmentList segList{adpSet->GetSegmentList()};

    uint64_t duration;
    if (XML::QueryAttrib(nodeSeglist, "duration", duration))
      segList.SetDuration(duration);

    uint32_t timescale;
    if (XML::QueryAttrib(nodeSeglist, "timescale", timescale))
      segList.SetTimescale(timescale);

    uint64_t presTimeOffset;
    if (XML::QueryAttrib(nodeSeglist, "presentationTimeOffset", presTimeOffset))
      segList.SetPresTimeOffset(presTimeOffset);

    uint64_t startNumber;
    if (XML::QueryAttrib(nodeSeglist, "startNumber", startNumber))
      segList.SetStartNumber(startNumber);

    adpSet->SetSegmentList(segList);

    // Parse <SegmentList> <SegmentTimeline> child
    xml_node nodeSegTL = nodeSeglist.child("SegmentTimeline");
    if (nodeSegTL)
    {
      ParseTagSegmentTimeline(nodeSegTL, adpSet->SegmentTimelineDuration());
    }
  }

  // Parse <SegmentDurations> tag
  // No dash spec, looks like a custom Amazon video service implementation
  // used to define the duration of each SegmentURL in the SegmentList
  xml_node nodeSegDur = nodeAdp.child("SegmentDurations");
  if (nodeSegDur)
  {
    uint64_t timescale;
    if (XML::QueryAttrib(nodeSegDur, "timescale", timescale))
      adpSet->SetSegDurationsTimescale(timescale);

    // Parse <S> tags - e.g. <S d="90000"/>
    // add all duration values as timeline segments
    for (xml_node node : nodeSegDur.children("S"))
    {
      adpSet->SegmentTimelineDuration().emplace_back(XML::GetAttribUint32(node, "d"));
    }
  }

  // Parse <ContentProtection> child tags
  if (nodeAdp.child("ContentProtection"))
  {
    period->SetEncryptionState(EncryptionState::NOT_SUPPORTED);
    ParseTagContentProtection(nodeAdp, adpSet->ProtectionSchemes());
    period->SetSecureDecodeNeeded(ParseTagContentProtectionSecDec(nodeAdp));
  }

  // Parse <Representation> child tags
  for (xml_node node : nodeAdp.children("Representation"))
  {
    ParseTagRepresentation(node, adpSet.get(), period);
  }

  if (adpSet->GetRepresentations().empty())
  {
    LOG::LogF(LOGWARNING, "Skipped AdaptationSet with id: \"%s\", has no representations.",
              adpSet->GetId().data());
    return;
  }

  // Copy codecs in the adaptation set to make MergeAdpSets more effective
  if (adpSet->GetCodecs().empty())
  {
    adpSet->AddCodecs(adpSet->GetRepresentations().front()->GetCodecs());
  }

  period->AddAdaptationSet(adpSet);
}

void adaptive::CDashTree::ParseTagRepresentation(pugi::xml_node nodeRepr,
                                                 PLAYLIST::CAdaptationSet* adpSet,
                                                 PLAYLIST::CPeriod* period)
{
  std::unique_ptr<CRepresentation> repr = CRepresentation::MakeUniquePtr(adpSet);

  repr->SetStartNumber(adpSet->GetStartNumber());
  repr->assured_buffer_duration_ = m_settings.m_bufferAssuredDuration;
  repr->max_buffer_duration_ = m_settings.m_bufferMaxDuration;

  repr->SetId(XML::GetAttrib(nodeRepr, "id"));

  repr->SetBandwidth(XML::GetAttribUint32(nodeRepr, "bandwidth"));

  repr->SetResWidth(XML::GetAttribInt(nodeRepr, "width"));
  repr->SetResHeight(XML::GetAttribInt(nodeRepr, "height"));

  std::string frameRateStr;
  if (XML::QueryAttrib(nodeRepr, "frameRate", frameRateStr))
  {
    uint32_t frameRate{0};
    uint32_t frameRateScale{1}; // Default 1 when attribute value has framerate without scale
    std::sscanf(frameRateStr.c_str(), "%" SCNu32 "/%" SCNu32, &frameRate, &frameRateScale);
    repr->SetFrameRate(frameRate);
    repr->SetFrameRateScale(frameRateScale);
  }

  std::string mimeType;
  if (XML::QueryAttrib(nodeRepr, "mimeType", mimeType))
  {
    repr->SetMimeType(mimeType);
    repr->SetContainerType(DetectContainerType(mimeType));
  }

  std::string codecs;
  if (XML::QueryAttrib(nodeRepr, "codecs", codecs))
    repr->AddCodecs(codecs);
  else
    repr->AddCodecs(adpSet->GetCodecs());

  if (repr->GetCodecs().empty())
    repr->AddCodecs(DetectCodecFromMimeType(repr->GetMimeType()));

  if (repr->GetCodecs().empty())
  {
    LOG::LogF(LOGWARNING,
              "Cannot get codecs for representation with id: \"%s\". Representation skipped.",
              repr->GetId().data());
    return;
  }

  // If AdaptationSet tag dont provide any info to know the content type
  // we attempt to determine it based on the content of the representation
  if (adpSet->GetStreamType() == StreamType::NOTYPE)
  {
    StreamType streamType = DetectStreamType("", repr->GetMimeType());
    const auto& codecs = repr->GetCodecs();
    if (streamType == StreamType::NOTYPE)
    {
      // Try find stream type by checking the codec string
      for (const std::string& codec : codecs)
      {
        if (CODEC::IsSubtitleFourCC(codec))
        {
          streamType = StreamType::SUBTITLE;
          break;
        }
      }
    }

    adpSet->SetStreamType(streamType);

    if (streamType == StreamType::SUBTITLE &&
        repr->GetMimeType() != "application/mp4") // Text format type only, not ISOBMFF
      repr->SetContainerType(ContainerType::TEXT);
  }

  // ISA custom attribute
  // No dash spec, looks like a custom Amazon video service implementation
  repr->SetCodecPrivateData(
      UTILS::AnnexbToAvc(XML::GetAttrib(nodeRepr, "codecPrivateData").data()));

  // ISA custom attribute
  repr->SetSampleRate(XML::GetAttribUint32(nodeRepr, "audioSamplingRate"));

  // ISA custom attribute
  uint32_t hdcp;
  if (XML::QueryAttrib(nodeRepr, "hdcp", hdcp))
    repr->SetHdcpVersion(static_cast<uint16_t>(hdcp));

  // Parse <BaseURL> tag
  std::string_view baseUrl = nodeRepr.child("BaseURL").child_value();
  //! @TODO: Multi BaseURL tag is not supported/implemented yet.
  //! There are two cases:
  //! 1) BaseURL without properties
  //!  <BaseURL>https://cdnurl1/</BaseURL>
  //!  the player must select the first base url by default and fallback
  //!  to the others when an address no longer available or not reachable.
  //! 2) BaseURL with DVB properties (ETSI TS 103 285 - DVB)
  //!  <BaseURL dvb:priority="1" dvb:weight="10" serviceLocation="A" >https://cdnurl1/</BaseURL>
  //!  where these properties affect the behaviour of the url selection.
  if (baseUrl.empty())
  {
    repr->SetBaseUrl(adpSet->GetBaseUrl());
  }
  else
  {
    if (URL::IsUrlAbsolute(baseUrl))
      repr->SetBaseUrl(baseUrl);
    else
      repr->SetBaseUrl(URL::Join(adpSet->GetBaseUrl().data(), baseUrl.data()));
  }

  // Parse <SegmentBase> tag
  xml_node nodeSegBase = nodeRepr.child("SegmentBase");
  if (nodeSegBase)
  {
    CSegmentBase segBase;
    std::string indexRange;
    if (XML::QueryAttrib(nodeSegBase, "indexRange", indexRange))
      segBase.SetIndexRange(indexRange);

    if (XML::GetAttrib(nodeSegBase, "indexRangeExact") == "true")
      segBase.SetIsRangeExact(true);

    uint32_t timescale;
    if (XML::QueryAttrib(nodeSegBase, "timescale", timescale))
    {
      segBase.SetTimescale(timescale);
      repr->SetTimescale(timescale);
    }

    // Parse <SegmentBase> <Initialization> child tag
    xml_node nodeInit = nodeSegBase.child("Initialization");
    if (nodeInit)
    {
      std::string range;
      if (XML::QueryAttrib(nodeInit, "range", range))
        segBase.SetInitRange(range);

      repr->SetInitSegment(segBase.MakeInitSegment());
    }

    repr->SetSegmentBase(segBase);
  }

  // Parse <SegmentTemplate> tag
  xml_node nodeSegTpl = nodeRepr.child("SegmentTemplate");
  if (nodeSegTpl || adpSet->HasSegmentTemplate())
  {
    CSegmentTemplate segTemplate{adpSet->GetSegmentTemplate()};

    if (nodeSegTpl)
      ParseSegmentTemplate(nodeSegTpl, segTemplate);

    repr->SetSegmentTemplate(segTemplate);

    if (segTemplate.HasInitialization())
      repr->SetInitSegment(segTemplate.MakeInitSegment());

    repr->SetStartNumber(segTemplate.GetStartNumber());
  }

  // Parse <SegmentList> tag
  xml_node nodeSeglist = nodeRepr.child("SegmentList");
  if (nodeSeglist)
  {
    CSegmentList segList{adpSet->GetSegmentList()};

    uint64_t duration;
    if (XML::QueryAttrib(nodeSeglist, "duration", duration))
      segList.SetDuration(duration);

    uint32_t timescale;
    if (XML::QueryAttrib(nodeSeglist, "timescale", timescale))
      segList.SetTimescale(timescale);

    uint64_t pto;
    if (XML::QueryAttrib(nodeSeglist, "presentationTimeOffset", pto))
      segList.SetPresTimeOffset(pto);

    uint64_t startNumber;
    if (XML::QueryAttrib(nodeSeglist, "startNumber", startNumber))
      segList.SetStartNumber(startNumber);

    if (segList.GetStartNumber() > 0)
      repr->SetStartNumber(segList.GetStartNumber());

    // Parse <SegmentList> <Initialization> child tag
    xml_node nodeInit = nodeSeglist.child("Initialization");
    if (nodeInit)
    {
      std::string range;
      if (XML::QueryAttrib(nodeInit, "range", range))
        segList.SetInitRange(range);

      std::string sourceURL;
      if (XML::QueryAttrib(nodeInit, "sourceURL", sourceURL))
        segList.SetInitSourceUrl(sourceURL);

      repr->SetInitSegment(segList.MakeInitSegment());
    }

    // Parse <SegmentList> <SegmentURL> child tags
    size_t index{0};
    uint64_t segStartPts{0};
    uint64_t segNumber = segList.GetStartNumber();

    // If <SegmentDurations> tag is present it could use a different timescale
    const size_t TLDurationSize = adpSet->SegmentTimelineDuration().size();
    const bool isTLDurTsRescale = adpSet->HasSegmentTimelineDuration() &&
                                  adpSet->GetSegDurationsTimescale() != NO_VALUE &&
                                  adpSet->GetSegDurationsTimescale() != segList.GetTimescale();

    for (xml_node node : nodeSeglist.children("SegmentURL"))
    {
      CSegment seg;

      std::string media;
      if (XML::QueryAttrib(node, "media", media))
        seg.url = media;

      uint64_t rangeStart{0};
      uint64_t rangeEnd{0};
      if (ParseRangeRFC(XML::GetAttrib(node, "mediaRange"), rangeStart, rangeEnd))
      {
        seg.range_begin_ = rangeStart;
        seg.range_end_ = rangeEnd;
      }

      uint64_t duration;
      if (TLDurationSize > 0 && index < TLDurationSize)
      {
        duration = adpSet->SegmentTimelineDuration()[index];
        if (isTLDurTsRescale)
        {
          duration =
              static_cast<uint64_t>(static_cast<double>(duration) /
                                    adpSet->GetSegDurationsTimescale() * segList.GetTimescale());
        }
      }
      else
        duration = segList.GetDuration();

      seg.startPTS_ = segStartPts;
      seg.m_endPts = seg.startPTS_ + duration;
      seg.m_time = segStartPts;
      seg.m_number = segNumber++;

      repr->Timeline().Add(seg);

      segStartPts += duration;
      index++;
    }

    repr->SetTimescale(segList.GetTimescale());

    repr->SetSegmentList(segList);
  }

  // Parse <ContentProtection> child tags
  if (nodeRepr.child("ContentProtection"))
  {
    period->SetEncryptionState(EncryptionState::NOT_SUPPORTED);
    ParseTagContentProtection(nodeRepr, repr->ProtectionSchemes());
  }

  // Store the protection data
  if (adpSet->HasProtectionSchemes() || repr->HasProtectionSchemes())
  {
    std::vector<uint8_t> pssh;
    std::string kid;
    std::string licenseUrl;
    // If a custom init PSSH is provided, should mean that a certain content protection tag
    // is missing, in this case we ignore the content protection tags and we add a PsshSet without data
    if (m_isCustomInitPssh || GetProtectionData(adpSet->ProtectionSchemes(),
                                                repr->ProtectionSchemes(), pssh, kid, licenseUrl))
    {
      period->SetEncryptionState(EncryptionState::ENCRYPTED_DRM);

      uint16_t psshSetPos =
          InsertPsshSet(adpSet->GetStreamType(), period, adpSet, pssh, kid, licenseUrl);

      if (psshSetPos == PSSHSET_POS_INVALID)
      {
        LOG::LogF(LOGWARNING, "Skipped representation with id: \"%s\", due to not valid PSSH",
                  repr->GetId().data());
        return;
      }
      repr->m_psshSetPos = psshSetPos;

      if (ParseTagContentProtectionSecDec(nodeRepr))
      {
        LOG::LogF(LOGERROR, "The <ContentProtection><widevine:license> tag must be child of "
                            "the <AdaptationSet> tag.");
      }
    }
  }

  // Parse <AudioChannelConfiguration> tag
  xml_node nodeAudioCh = nodeRepr.child("AudioChannelConfiguration");
  if (nodeAudioCh)
    adpSet->SetAudioChannels(ParseAudioChannelConfig(nodeAudioCh));
  else if (adpSet->GetStreamType() == StreamType::AUDIO && repr->GetAudioChannels() == 0)
    repr->SetAudioChannels(2); // Fallback to 2 channels when no value is set

  // Parse <SupplementalProperty> child tags
  for (xml_node nodeSP : nodeRepr.children("SupplementalProperty"))
  {
    std::string_view schemeIdUri = XML::GetAttrib(nodeSP, "schemeIdUri");
    std::string_view value = XML::GetAttrib(nodeSP, "value");

    if (schemeIdUri == "tag:dolby.com,2018:dash:EC3_ExtensionType:2018")
    {
      if (value == "JOC")
        repr->AddCodecs(CODEC::NAME_EAC3_JOC);
    }
    else if (schemeIdUri == "tag:dolby.com,2018:dash:EC3_ExtensionComplexityIndex:2018")
    {
      uint32_t channels = STRING::ToUint32(value);
      if (channels > 0)
        repr->SetAudioChannels(channels);
    }
    else if (schemeIdUri == "http://dashif.org/guidelines/last-segment-number")
    {
      repr->SetSegmentEndNr(STRING::ToUint64(value));
    }
  }

  if (repr->GetContainerType() == ContainerType::TEXT && repr->GetMimeType() != "application/mp4" &&
      !repr->HasSegmentBase() && !repr->HasSegmentTemplate() && repr->Timeline().IsEmpty())
  {
    // Raw unsegmented subtitles called "sidecar" is a single file specified in the <BaseURL> tag,
    // must not have the MP4 ISOBMFF mime type or any other dash element.
    repr->SetIsSubtitleFileStream(true);
  }

  // Generate segments from SegmentTemplate
  if (repr->HasSegmentTemplate() && repr->Timeline().IsEmpty())
  {
    auto& segTemplate = repr->GetSegmentTemplate();

    if (segTemplate->GetMedia().empty())
    {
      LOG::LogF(LOGWARNING,
                "Cannot generate segments timeline, SegmentTemplate has no media attribute.");
    }
    else if (segTemplate->GetTimescale() == 0)
    {
      LOG::LogF(LOGWARNING,
                "Cannot generate segments timeline, SegmentTemplate has no timescale attribute.");
    }
    else if (segTemplate->GetDuration() == 0 && !segTemplate->HasTimeline())
    {
      // In the SegmentTemplate tag must be present the "duration" attribute or the SegmentTimeline tag
      LOG::LogF(LOGWARNING,
                "Cannot generate segments timeline, SegmentTemplate has no duration attribute.");
    }
    else
    {
      uint64_t segNumber = segTemplate->GetStartNumber();
      const bool hasMediaNumber = segTemplate->HasMediaNumber();
      const uint32_t segTimescale = segTemplate->GetTimescale();
      const uint64_t periodStartMs = period->GetStart() == NO_VALUE ? 0 : period->GetStart();
      const uint64_t periodStartScaled = periodStartMs * segTemplate->GetTimescale() / 1000;

      //! @todo: PTO a/v sync to be implemented on session/demuxers
      const bool hasPTO = segTemplate->HasPresTimeOffset();

      if (segTemplate->HasTimeline()) // Generate segments from template timeline
      {
        uint64_t time{0};

        for (const auto& tlElem : segTemplate->Timeline())
        {
          uint32_t repeat = tlElem.repeat;
          if (tlElem.time > 0)
            time = tlElem.time;

          do
          {
            CSegment seg;
            seg.startPTS_ = time;
            // If no PTO, the "t" value on <SegmentTimeline><S> element should be relative to period start
            // this may be wrong, has been added to try fix following sample stream
            // https://d24rwxnt7vw9qb.cloudfront.net/v1/dash/e6d234965645b411ad572802b6c9d5a10799c9c1/All_Reference_Streams//6e16c26536564c2f9dbc5f725a820cff/index.mpd
            if (!hasPTO)
              seg.startPTS_ += periodStartScaled;
            seg.m_endPts = seg.startPTS_ + tlElem.duration;

            if (hasMediaNumber)
              seg.m_number = segNumber++;

            seg.m_time = time;

            repr->Timeline().Add(seg);

            time += tlElem.duration;
          } while (repeat-- > 0);
        }

        repr->SetTimescale(segTimescale);
      }
      else // Generate segments by using template
      {
        // Determines number of segments to be generated
        size_t segmentsCount = 1;

        const uint32_t segDuration = segTemplate->GetDuration();
        const uint64_t segDurMs = static_cast<uint64_t>(segDuration) * 1000 / segTimescale;
        uint64_t time = periodStartScaled;

        uint64_t periodDurMs = period->GetDuration() * 1000 / period->GetTimescale();
        if (periodDurMs == 0)
          periodDurMs = m_mediaPresDuration;

        // Generate segments from TSB
        uint64_t tsbStart = (stream_start_ - available_time_) - m_timeShiftBufferDepth;
        uint64_t tsbEnd = tsbStart + m_timeShiftBufferDepth;
        if (m_timeShiftBufferDepth > 0 && tsbEnd > periodStartMs)
        {
          if (tsbStart < periodStartMs && !m_periods.empty())
            tsbStart = periodStartMs;

          if (periodDurMs > 0 && tsbStart >= periodStartMs)
            tsbStart = periodStartMs;

          if (periodDurMs > 0 && tsbEnd > periodStartMs + periodDurMs)
            tsbEnd = periodStartMs + periodDurMs;

          const uint64_t durationMs = tsbEnd - tsbStart;

          segmentsCount = std::max<size_t>(durationMs / segDurMs, 1);

          if (available_time_ == 0)
          {
            time = tsbStart * segTemplate->GetTimescale() / 1000;
            segNumber = tsbStart / segDurMs;
          }
          else
          {
            time += tsbStart * segTemplate->GetTimescale() / 1000;
            segNumber += tsbStart / segDurMs;
          }
        }
        else if (periodDurMs > 0)
        {
          segmentsCount =
              static_cast<size_t>(std::ceil(static_cast<double>(periodDurMs) / segDurMs));
        }

        // If signalled limit number of segments to the end segment number
        uint64_t segNumberEnd = SEGMENT_NO_NUMBER;
        if (segTemplate->HasEndNumber())
          segNumberEnd = segTemplate->GetEndNumber();
        else if (repr->HasSegmentEndNr())
          segNumberEnd = repr->GetSegmentEndNr();

        for (size_t i = 0; i < segmentsCount; ++i)
        {
          if (segNumber > segNumberEnd)
            break;

          CSegment seg;
          seg.startPTS_ = time;
          seg.m_endPts = seg.startPTS_ + segDuration;

          if (hasMediaNumber)
            seg.m_number = segNumber++;

          seg.m_time = time;

          repr->Timeline().Add(seg);

          time = seg.m_endPts;
        }

        repr->SetTimescale(segTemplate->GetTimescale());
      }
    }
  }

  repr->SetDuration(repr->Timeline().GetDuration());
  repr->SetScaling();

  adpSet->AddRepresentation(repr);
}

void adaptive::CDashTree::ParseTagSegmentTimeline(pugi::xml_node nodeSegTL,
                                                  std::vector<uint32_t>& SCTimeline)
{
  uint64_t nextPts{0};

  // Parse <S> tags - e.g. <S t="3600" d="900000" r="2398"/>
  for (xml_node node : nodeSegTL.children("S"))
  {
    uint64_t time = XML::GetAttribUint64(node, "t");
    uint32_t duration = XML::GetAttribUint32(node, "d");
    uint32_t repeat = XML::GetAttribUint32(node, "r");
    repeat += 1;

    if (SCTimeline.empty())
    {
      nextPts = time;
    }
    else if (time > 0)
    {
      //Go back to the previous timestamp to calculate the real gap.
      nextPts -= SCTimeline.back();
      SCTimeline.back() = static_cast<uint32_t>(time - nextPts);
      nextPts = time;
    }
    if (duration > 0)
    {
      for (; repeat > 0; --repeat)
      {
        SCTimeline.emplace_back(duration);
        nextPts += duration;
      }
    }
  }
}

void adaptive::CDashTree::ParseSegmentTemplate(pugi::xml_node node, CSegmentTemplate& segTpl)
{
  uint32_t timescale;
  if (XML::QueryAttrib(node, "timescale", timescale))
    segTpl.SetTimescale(timescale);

  if (segTpl.GetTimescale() == 0)
    segTpl.SetTimescale(1); // if not specified defaults to seconds

  uint32_t duration;
  if (XML::QueryAttrib(node, "duration", duration))
    segTpl.SetDuration(duration);

  std::string media;
  if (XML::QueryAttrib(node, "media", media))
    segTpl.SetMedia(media);

  uint32_t startNumber;
  if (XML::QueryAttrib(node, "startNumber", startNumber))
    segTpl.SetStartNumber(startNumber);

  uint64_t endNumber;
  if (XML::QueryAttrib(node, "endNumber", endNumber))
    segTpl.SetEndNumber(endNumber);

  std::string initialization;
  if (XML::QueryAttrib(node, "initialization", initialization))
    segTpl.SetInitialization(initialization);

  uint64_t pto;
  if (XML::QueryAttrib(node, "presentationTimeOffset", pto))
    segTpl.SetPresTimeOffset(pto);

  // Parse <SegmentTemplate> <SegmentTimeline> child
  xml_node nodeSegTL = node.child("SegmentTimeline");
  if (nodeSegTL)
  {
    // If a parent SegmentTemplate contains a SegmentTimeline, delete it
    segTpl.Timeline().clear();

    // Parse <SegmentTemplate><SegmentTimeline>, <S> elements
    // e.g. <S t="3600" d="900000" r="2398"/>
    for (xml_node node : nodeSegTL.children("S"))
    {
      CSegmentTemplate::TimelineElement tlElem;

      XML::QueryAttrib(node, "t", tlElem.time);
      XML::QueryAttrib(node, "d", tlElem.duration);
      XML::QueryAttrib(node, "r", tlElem.repeat);

      if (tlElem.duration == 0)
      {
        LOG::LogF(LOGDEBUG, "Skip <SegmentTimeline> <S> element, missing duration.");
        continue;
      }

      segTpl.Timeline().emplace_back(tlElem);
    }
  }
}

void adaptive::CDashTree::ParseTagContentProtection(
    pugi::xml_node nodeParent, std::vector<PLAYLIST::ProtectionScheme>& protSchemes)
{
  // Parse each ContentProtection tag to collect encryption schemes
  for (xml_node nodeCP : nodeParent.children("ContentProtection"))
  {
    std::string_view schemeIdUri = XML::GetAttrib(nodeCP, "schemeIdUri");

    ProtectionScheme protScheme;
    protScheme.idUri = schemeIdUri;
    protScheme.value = XML::GetAttrib(nodeCP, "value");

    // Get optional default KID
    // Parse first attribute that end with "... default_KID"
    // e.g. cenc:default_KID="01004b6f-0835-b807-9098-c070dc30a6c7"
    xml_attribute attrKID = XML::FirstAttributeNoPrefix(nodeCP, "default_KID");
    if (attrKID)
      protScheme.kid = attrKID.value();

    // Parse child tags
    for (xml_node node : nodeCP.children())
    {
      std::string childName = node.name();

      if (StringUtils::EndsWith(childName, "pssh")) // e.g. <cenc:pssh> or <pssh> ...
      {
        protScheme.pssh = node.child_value();
      }
      else if (StringUtils::EndsWithNoCase(childName, "laurl")) // e.g. <clearkey:Laurl> or <dashif:Laurl> ...
      {
        protScheme.licenseUrl = node.child_value();
      }
      else if (childName == "mspr:pro" || childName == "pro")
      {
        PRProtectionParser parser;
        if (parser.ParseHeader(node.child_value()))
          protScheme.kid = STRING::ToHexadecimal(parser.GetKID());
      }
    }

    protSchemes.emplace_back(protScheme);
  }
}

bool adaptive::CDashTree::GetProtectionData(
    const std::vector<PLAYLIST::ProtectionScheme>& adpProtSchemes,
    const std::vector<PLAYLIST::ProtectionScheme>& reprProtSchemes,
    std::vector<uint8_t>& pssh,
    std::string& kid,
    std::string& licenseUrl)
{
  // Try find a protection scheme compatible for the current systemid
  const ProtectionScheme* protSelected = nullptr;
  const ProtectionScheme* protCommon = nullptr;

  for (std::string_view supportedKeySystem : m_supportedKeySystems)
  {
    for (const ProtectionScheme& protScheme : reprProtSchemes)
    {
      if (STRING::CompareNoCase(protScheme.idUri, supportedKeySystem))
      {
        protSelected = &protScheme;
      }
      else if (protScheme.idUri == "urn:mpeg:dash:mp4protection:2011")
      {
        protCommon = &protScheme;
      }
    }

    if (!protSelected || !protCommon)
    {
      for (const ProtectionScheme& protScheme : adpProtSchemes)
      {
        if (!protSelected && STRING::CompareNoCase(protScheme.idUri, supportedKeySystem))
        {
          protSelected = &protScheme;
        }
        else if (!protCommon && protScheme.idUri == "urn:mpeg:dash:mp4protection:2011")
        {
          protCommon = &protScheme;
        }
      }
    }
  }

  // Workaround for ClearKey:
  // if license type ClearKey is set and a manifest dont contains ClearKey protection scheme
  // in any case the KID is required to allow decryption (with clear keys or license URLs provided by Kodi props)
  //! @todo: this should not be a task of parser, moreover missing an appropriate KID extraction from mp4 box
  auto& kodiProps = CSrvBroker::GetKodiProps();
  ProtectionScheme ckProtScheme;
  if (kodiProps.GetLicenseType() == DRM::KS_CLEARKEY)
  {
    std::string_view defaultKid;
    if (protSelected)
      defaultKid = protSelected->kid;
    if (defaultKid.empty() && protCommon)
      defaultKid = protCommon->kid;

    if (defaultKid.empty())
    {
      for (const ProtectionScheme& protScheme : reprProtSchemes)
      {
        if (!protScheme.kid.empty())
        {
          defaultKid = protScheme.kid;
          break;
        }
      }
      if (defaultKid.empty())
      {
        for (const ProtectionScheme& protScheme : adpProtSchemes)
        {
          if (!protScheme.kid.empty())
          {
            defaultKid = protScheme.kid;
            break;
          }
        }
      }
      if (protCommon)
        ckProtScheme = *protCommon;

      ckProtScheme.kid = defaultKid;
      protCommon = &ckProtScheme;
    }
  }

  bool isEncrypted{false};
  std::string selectedKid;
  std::string selectedPssh;

  if (protSelected)
  {
    isEncrypted = true;
    selectedKid = protSelected->kid;
    selectedPssh = protSelected->pssh;
    licenseUrl = protSelected->licenseUrl;
  }
  if (protCommon)
  {
    isEncrypted = true;
    if (selectedKid.empty())
      selectedKid = protCommon->kid;

    // Set crypto mode
    if (protCommon->value == "cenc")
      m_cryptoMode = CryptoMode::AES_CTR;
    else if (protCommon->value == "cbcs")
      m_cryptoMode = CryptoMode::AES_CBC;
  }

  if (!selectedPssh.empty())
    pssh = BASE64::Decode(selectedPssh);

  // There are no constraints on the Kid format, it is recommended to be as UUID but not mandatory
  STRING::ReplaceAll(selectedKid, "-", "");
  kid = selectedKid;

  return isEncrypted;
}

bool adaptive::CDashTree::ParseTagContentProtectionSecDec(pugi::xml_node nodeParent)
{
  // Try to find ISA custom tag/attrib:
  // <ContentProtection><widevine:license robustness_level="HW_SECURE_CODECS_REQUIRED">
  // to know if its needed to force the secure decoder
  for (xml_node nodeCP : nodeParent.children("ContentProtection"))
  {
    // Parse child tags
    for (xml_node node : nodeCP.children())
    {
      if (STRING::Compare(node.name(), "widevine:license"))
      {
        // <widevine:license robustness_level="HW_SECURE_CODECS_REQUIRED"> Custom ISA tag
        // to force secure decoder, accepted in the <AdaptationSet> only

        //! @TODO: Since this param is set to Period, we could think to deprecate
        //! this support and add a custom tag in the Period itself
        std::string_view robustnessLevel = XML::GetAttrib(nodeCP, "robustness_level");
        if (robustnessLevel == "HW")
        {
          LOG::LogF(LOGWARNING, "The value \"HW\" of attribute \"robustness_level\" in "
                                "<widevine:license> tag is now deprecated. "
                                "You must change it to \"HW_SECURE_CODECS_REQUIRED\".");
          robustnessLevel = "HW_SECURE_CODECS_REQUIRED";
        }
        return robustnessLevel == "HW_SECURE_CODECS_REQUIRED";
      }
    }
  }
  return false;
}

uint32_t adaptive::CDashTree::ParseAudioChannelConfig(pugi::xml_node node)
{
  std::string_view schemeIdUri = XML::GetAttrib(node, "schemeIdUri");
  std::string_view value = XML::GetAttrib(node, "value");
  uint32_t channels{0};

  if (schemeIdUri == "urn:mpeg:dash:outputChannelPositionList:2012")
  {
    // A space-separated list of speaker positions,
    // the number of channels is the length of the list
    return static_cast<uint32_t>(STRING::SplitToVec(value, ' ').size());
  }
  else if (schemeIdUri == "urn:mpeg:dash:23003:3:audio_channel_configuration:2011" ||
           schemeIdUri == "urn:dts:dash:audio_channel_configuration:2012")
  {
    // The value is the number of channels
    channels = STRING::ToUint32(value);
  }
  else if (schemeIdUri == "urn:dolby:dash:audio_channel_configuration:2011" ||
           schemeIdUri == "tag:dolby.com,2014:dash:audio_channel_configuration:2011")
  {
    // A hex-encoded 16-bit integer, each bit represents a channel
    uint32_t hexVal = STRING::HexStrToUint(value);
    uint32_t numBits{0};
    while (hexVal)
    {
      if (hexVal & 1)
      {
        ++numBits;
      }
      hexVal = hexVal >> 1;
    }
    channels = numBits;
  }
  else if (schemeIdUri == "urn:mpeg:mpegB:cicp:ChannelConfiguration")
  {
    // Defined by https://dashif.org/identifiers/audio_source_metadata/
    static const size_t mapSize = 21;
    static const int channelCountMapping[mapSize]{
        0,  1, 2, 3,  4, 5,  6,  8,  2,  3, /* 0--9 */
        4,  7, 8, 24, 8, 12, 10, 12, 14, 12, /* 10--19 */
        14, /* 20 */
    };
    uint32_t pos = STRING::ToUint32(value);
    if (pos > 0 && pos < mapSize)
    {
      channels = channelCountMapping[pos];
    }
  }
  if (channels == 0)
  {
    LOG::LogF(LOGWARNING, "Cannot parse channel configuration \"%s\", fallback to 2 channels.",
              schemeIdUri.data());
    channels = 2;
  }
  return channels;
}

//! @todo: MergeAdpSets its a kind of workaround
//! its missing a middle interface where store "streams" (or media tracks) data in a form
//! that is detached from "tree" interface, this would avoid the force
//! change of CAdaptationSet data and its parent data (CRepresentation::SetParent)
void adaptive::CDashTree::MergeAdpSets()
{
  // NOTE: This method wipe out all properties of merged adaptation set
  for (auto itPeriod = m_periods.begin(); itPeriod != m_periods.end(); ++itPeriod)
  {
    auto period = itPeriod->get();
    auto& periodAdpSets = period->GetAdaptationSets();
    for (auto itAdpSet = periodAdpSets.begin(); itAdpSet != periodAdpSets.end(); ++itAdpSet)
    {
      CAdaptationSet* adpSet = itAdpSet->get();
      for (auto itNextAdpSet = itAdpSet + 1; itNextAdpSet != periodAdpSets.end();)
      {
        CAdaptationSet* nextAdpSet = itNextAdpSet->get();
        // IsMergeable:
        //  Some services (e.g. amazon) may have several AdaptationSets of the exact same audio track
        //  the only difference is in the ContentProtection selectedKid/selectedPssh and the base url,
        //  in order not to show several identical audio tracks in the Kodi GUI, we must merge adaptation sets
        // CompareSwitchingId:
        //  Some services can provide switchable video adp sets, these could havedifferent codecs, and could be
        //  used to split HD resolutions from SD, so to allow Chooser's to autoselect the video quality
        //  we need to merge them all
        // CODEC NOTE: since we cannot know in advance the supported video codecs by the hardware in use
        //  we cannot merge adp sets with different codecs otherwise playback will not work
        if (adpSet->CompareSwitchingId(nextAdpSet) || adpSet->IsMergeable(nextAdpSet))
        {
          // Sanitize adaptation set references to selectedPssh sets
          for (CPeriod::PSSHSet& psshSet : period->GetPSSHSets())
          {
            if (psshSet.adaptation_set_ == nextAdpSet)
              psshSet.adaptation_set_ = adpSet;
          }
          // Move representations to the first switchable adaptation set
          for (auto itRepr = nextAdpSet->GetRepresentations().begin();
               itRepr < nextAdpSet->GetRepresentations().end(); ++itRepr)
          {
            itRepr->get()->SetParent(adpSet, true);
            adpSet->GetRepresentations().push_back(std::move(*itRepr));
          }
          itNextAdpSet = periodAdpSets.erase(itNextAdpSet);
        }
        else
          ++itNextAdpSet;
      }
    }
  }
}

bool adaptive::CDashTree::DownloadManifestUpd(std::string_view url,
                                              const std::map<std::string, std::string>& reqHeaders,
                                              const std::vector<std::string>& respHeaders,
                                              UTILS::CURL::HTTPResponse& resp)
{
  return CURL::DownloadFile(url, reqHeaders, respHeaders, resp);
}

void adaptive::CDashTree::OnRequestSegments(PLAYLIST::CPeriod* period,
                                            PLAYLIST::CAdaptationSet* adp,
                                            PLAYLIST::CRepresentation* rep)
{
  if (adp->GetStreamType() == StreamType::VIDEO || adp->GetStreamType() == StreamType::AUDIO)
  {
    OnUpdateSegments();
  }
}

//! @todo: check updated variables that are not thread safe
void adaptive::CDashTree::OnUpdateSegments()
{
  lastUpdated_ = std::chrono::system_clock::now();

  std::unique_ptr<CDashTree> updateTree{std::move(Clone())};

  // Custom manifest update url parameters
  std::string manifestParams = m_manifestUpdParams;

  std::string manifestUrl;
  if (location_.empty())
  {
    manifestUrl = manifest_url_;
    if (!manifestParams.empty())
      manifestUrl = URL::RemoveParameters(manifestUrl);
  }
  else
    manifestUrl = location_;

  if (manifestParams.find("$START_NUMBER$") != std::string::npos)
  {
    // This was a old custom YouTube implementation no longer used
    LOG::LogF(LOGERROR,
              "The $START_NUMBER$ placeholder in the manifest parameters is no longer supported.");
  }

  // Set header data based from previous manifest request
  if (!m_manifestRespHeaders["etag"].empty())
    m_manifestHeaders["If-None-Match"] = "\"" + m_manifestRespHeaders["etag"] + "\"";

  if (!m_manifestRespHeaders["last-modified"].empty())
    m_manifestHeaders["If-Modified-Since"] = m_manifestRespHeaders["last-modified"];

  URL::AppendParameters(manifestUrl, manifestParams);

  // Download and open the manifest update
  CURL::HTTPResponse resp;
  if (!DownloadManifestUpd(manifestUrl, m_manifestHeaders, {"etag", "last-modified"}, resp) ||
      !updateTree->Open(resp.effectiveUrl, resp.headers, resp.data))
  {
    return;
  }

  // Update local members for the next manifest update
  m_manifestRespHeaders = resp.headers;
  location_ = updateTree->location_;

  for (size_t index{0}; index < updateTree->m_periods.size(); index++)
  {
    auto& updPeriod = updateTree->m_periods[index];

    // find matching period based on ID
    auto itPeriod =
        std::find_if(m_periods.begin(), m_periods.end(),
                      [&updPeriod](const std::unique_ptr<CPeriod>& item)
                      { return !item->GetId().empty() && item->GetId() == updPeriod->GetId(); });
    // if not found, try matching period based on start
    if (itPeriod == m_periods.end())
    {
      itPeriod =
          std::find_if(m_periods.begin(), m_periods.end(),
                        [&updPeriod](const std::unique_ptr<CPeriod>& item)
                        { return item->GetStart() != NO_VALUE && item->GetStart() == updPeriod->GetStart(); });
    }

    CPeriod* period{nullptr};

    if (itPeriod != m_periods.end())
      period = (*itPeriod).get();

    if (!period && updPeriod->GetId().empty() && (updPeriod->GetStart() == NO_VALUE))
    {
      // not found, fallback match based on position
      if (index < m_periods.size())
        period = m_periods[index].get();
    }

    // new period, insert it
    if (!period)
    {
      LOG::LogF(LOGDEBUG, "Inserting new Period (id=%s, start=%llu)", updPeriod->GetId().data(),
                updPeriod->GetStart());

      updPeriod->SetSequence(m_periodCurrentSeq++);
      m_periods.push_back(std::move(updPeriod));
      continue;
    }
    else // Update period data that may be added or changed
    {
      if (updPeriod->GetDuration() > 0)
        period->SetDuration(updPeriod->GetDuration());
    }

    for (auto& updAdpSet : updPeriod->GetAdaptationSets())
    {
      // Locate adaptationset
      if (!updAdpSet)
        continue;

      for (auto& adpSet : period->GetAdaptationSets())
      {
        if (!(adpSet->GetId() == updAdpSet->GetId() &&
              adpSet->GetGroup() == updAdpSet->GetGroup() &&
              adpSet->GetStreamType() == updAdpSet->GetStreamType() &&
              adpSet->GetMimeType() == updAdpSet->GetMimeType() &&
              adpSet->GetLanguage() == updAdpSet->GetLanguage()))
          continue;

        for (auto& updRepr : updAdpSet->GetRepresentations())
        {
          // Locate representation
          auto itRepr = std::find_if(adpSet->GetRepresentations().begin(),
                                      adpSet->GetRepresentations().end(),
                                      [&updRepr](const std::unique_ptr<CRepresentation>& item)
                                      { return item->GetId() == updRepr->GetId(); });

          // Found representation
          if (itRepr != adpSet->GetRepresentations().end())
          {
            auto repr = (*itRepr).get();

            if (updRepr->Timeline().IsEmpty())
            {
              LOG::LogF(LOGWARNING,
                        "MPD update - Updated timeline has no segments "
                        "(repr. id \"%s\", period id \"%s\")",
                        repr->GetId().data(), period->GetId().data());
              continue;
            }

            if (!repr->Timeline().IsEmpty())
            {
              if (!repr->current_segment_) // Representation that should not be used for playback
              {
                repr->Timeline().Swap(updRepr->Timeline());
              }
              else
              {
                if (repr->Timeline().GetInitialSize() == updRepr->Timeline().GetSize() &&
                    repr->Timeline().Get(0)->startPTS_ == updRepr->Timeline().Get(0)->startPTS_)
                {
                  LOG::LogF(LOGDEBUG,
                            "MPD update - No new segments (repr. id \"%s\", period id \"%s\")",
                            repr->GetId().data(), period->GetId().data());
                  continue;
                }

                const CSegment* foundSeg{nullptr};
                const uint64_t segStartPTS = repr->current_segment_->startPTS_;

                for (const CSegment& segment : updRepr->Timeline())
                {
                  if (segment.startPTS_ == segStartPTS)
                  {
                    foundSeg = &segment;
                    break;
                  }
                  else if (segment.startPTS_ > segStartPTS)
                  {
                    // Can fall here if video is paused and current segment is too old,
                    // or the video provider provide MPD updates that have misaligned PTS on segments,
                    // so small PTS gaps that prevent to find the same segment
                    const uint64_t segNumber = repr->current_segment_->m_number;
                    foundSeg = &segment;
                    LOG::LogF(LOGDEBUG,
                              "MPD update - Misaligned: current seg [PTS %llu, Number: %llu] "
                              "found [PTS %llu, Number %llu] "
                              "(repr. id \"%s\", period id \"%s\")",
                              segStartPTS, segNumber, segment.startPTS_, segment.m_number,
                              repr->GetId().data(), period->GetId().data());
                    break;
                  }
                }

                if (!foundSeg)
                {
                  LOG::LogF(LOGDEBUG,
                            "MPD update - No segment found (repr. id \"%s\", period id \"%s\")",
                            repr->GetId().data(), period->GetId().data());
                }
                else
                {
                  repr->Timeline().Swap(updRepr->Timeline());
                  repr->current_segment_ = foundSeg;

                  LOG::LogF(LOGDEBUG, "MPD update - Done (repr. id \"%s\", period id \"%s\")",
                            updRepr->GetId().data(), period->GetId().data());
                }
              }

              if (repr->IsWaitForSegment() && repr->GetNextSegment())
              {
                repr->SetIsWaitForSegment(false);
                LOG::LogF(LOGDEBUG, "End WaitForSegment repr. id %s", repr->GetId().data());
              }

              m_totalTime = updateTree->m_totalTime;
            }
          }
        }
      }
    }
  }
}

bool adaptive::CDashTree::InsertLiveSegment(PLAYLIST::CPeriod* period,
                                            PLAYLIST::CAdaptationSet* adpSet,
                                            PLAYLIST::CRepresentation* repr,
                                            size_t pos)
{
  //! @todo: seem this method should be used with manifests having SegmentTemplate without timeline only
  //! if so the code should use the SegmentTemplate info to generate the next segment.
  //! 
  //! For now has been limited to manifests that have SegmentTemplate without timeline,
  //! if in future there is no bad feedbacks from users, this can be cleaned up
  //! Note: As dash specs SegmentList is no longer supported for live streaming.

  if (HasManifestUpdatesSegs() || pos == SEGMENT_NO_POS || !repr->HasSegmentTemplate() ||
      repr->GetSegmentTemplate()->HasTimeline())
    return false;

  /* 
   * SegmentTimelineDuration is not used with SegmentTemplate,
   * InsertLiveSegment is always called only when there is no more segments
   * this could be removed
   * 
  // Check if its the last frame we watch
  if (!adpSet->SegmentTimelineDuration().IsEmpty())
  {
    if (pos == adpSet->SegmentTimelineDuration().GetSize() - 1)
    {
      adpSet->SegmentTimelineDuration().Append(
          static_cast<std::uint32_t>(fragmentDuration * period->GetTimescale() / movieTimescale));
    }
    else
    {
      repr->expired_segments_++;
      return false;
    }
  }
  else if (pos != repr->SegmentTimeline().GetSize() - 1)
    return false;
  */

  //! @todo: expired_segments_ should be reworked, see also other parsers
  repr->expired_segments_++;

  const CSegment* segment = repr->Timeline().Get(pos);

  if (!segment)
  {
    LOG::LogF(LOGERROR, "Segment at position %zu not found from representation id: %s", pos,
              repr->GetId().data());
    return false;
  }

  CSegment segCopy = *segment;
  uint64_t dur = segCopy.m_endPts - segCopy.startPTS_;
  segCopy.startPTS_ = segCopy.m_endPts;
  segCopy.m_endPts = segCopy.startPTS_ + dur;
  segCopy.m_time = segCopy.m_endPts;
  segCopy.m_number++;

  LOG::LogF(LOGDEBUG, "Insert live segment to adptation set \"%s\" (Start PTS: %llu, number: %llu)",
            adpSet->GetId().data(), segCopy.startPTS_, segCopy.m_number);

  for (auto& repr : adpSet->GetRepresentations())
  {
    repr->Timeline().Append(segCopy);
  }
  return true;
}

bool adaptive::CDashTree::InsertLiveFragment(PLAYLIST::CAdaptationSet* adpSet,
                                             PLAYLIST::CRepresentation* repr,
                                             uint64_t fTimestamp,
                                             uint64_t fDuration,
                                             uint32_t fTimescale)
{
  // MPD segment-controlled live should not have MPD@minimumUpdatePeriod
  // since its expected to parse segments packets to extract updates
  if (!m_isLive || !repr->HasSegmentTemplate() || m_minimumUpdatePeriod != NO_VALUE)
    return false;

  const CSegment* lastSeg = repr->Timeline().GetBack();
  if (!lastSeg)
    return false;

  LOG::Log(LOGDEBUG, "Fragment info - timestamp: %llu, duration: %llu, timescale: %u", fTimestamp,
           fDuration, fTimescale);

  const uint64_t fStartPts =
      static_cast<uint64_t>(static_cast<double>(fTimestamp) / fTimescale * repr->GetTimescale());

  if (fStartPts <= lastSeg->startPTS_)
    return false;

  repr->expired_segments_++;

  CSegment segCopy = *lastSeg;
  const uint64_t duration =
      static_cast<uint64_t>(static_cast<double>(fDuration) / fTimescale * repr->GetTimescale());

  segCopy.startPTS_ = fStartPts;
  segCopy.m_endPts = segCopy.startPTS_ + duration;
  segCopy.m_time = segCopy.startPTS_;
  segCopy.m_number++;

  LOG::Log(LOGDEBUG, "Insert fragment to adaptation set \"%s\" (PTS: %llu, number: %llu)",
           adpSet->GetId().data(), segCopy.startPTS_, segCopy.m_number);

  for (auto& repr : adpSet->GetRepresentations())
  {
    repr->Timeline().Append(segCopy);
  }

  return true;
}
