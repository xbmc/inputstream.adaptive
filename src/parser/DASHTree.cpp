/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DASHTree.h"

#include "CompKodiProps.h"
#include "SrvBroker.h"
#include "common/Period.h"
#include "decrypters/HelperPr.h"
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
 */

namespace
{
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
  if (STRING::Contains(mimeType, "/mp4"))
    return ContainerType::MP4;
  if (STRING::Contains(mimeType, "/webm"))
    return ContainerType::WEBM;
  if (STRING::Contains(mimeType, "/x-matroska"))
    return ContainerType::MATROSKA;
  if (STRING::Contains(mimeType, "/ttml+xml") || STRING::Contains(mimeType, "vtt"))
    return ContainerType::TEXT;

  return ContainerType::INVALID;
}

std::string DetectCodecFromMimeType(std::string_view mimeType)
{
  if (mimeType == "text/vtt")
    return CODEC::FOURCC_WVTT;
  if (mimeType == "application/ttml+xml")
    return CODEC::FOURCC_TTML;

  return "";
}

// \brief Make an HTTP request to get UTC Timing and return the string
std::string RequestUTCTiming(const std::string& url, CURL::RequestType reqType)
{
  CURL::CUrl curl(url, reqType);
  const int statusCode = curl.Open();
  if (statusCode == -1 || statusCode >= 400)
  {
    LOG::LogF(LOGERROR, "Unable to retrieve UTC timing from url: %s", url.c_str());
    return {};
  }

  std::string date;
  if (reqType == CURL::RequestType::HEAD)
  {
    date = curl.GetResponseHeader("date");
  }
  else
  {
    if (curl.Read(date) != CURL::ReadStatus::IS_EOF)
    {
      LOG::LogF(LOGERROR, "Unable to retrieve UTC timing data from url: (url: %s)", url.c_str());
      return {};
    }
  }
  return date;
}

} // unnamed namespace


adaptive::CDashTree::CDashTree(const CDashTree& left) : AdaptiveTree(left)
{
  m_clockOffset = left.m_clockOffset;
}

void adaptive::CDashTree::Configure(CHOOSER::IRepresentationChooser* reprChooser,
                                    const std::string& manifestUpdParams)
{
  AdaptiveTree::Configure(reprChooser, manifestUpdParams);
}

bool adaptive::CDashTree::Open(const std::string& url,
                               const std::map<std::string, std::string>& headers,
                               const std::string& data)
{
  SaveManifest("", data, url);

  m_manifestRespHeaders = headers;
  manifest_url_ = url;
  base_url_ = URL::GetUrlPath(url);

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
  uint64_t now = stream_start_ + *m_clockOffset - available_time_;
  if (m_isLive && !kodiProps.IsPlayTimeshift())
  {
    for (auto& period : m_periods)
    {
      if (period->GetStart() != NO_VALUE && now >= period->GetStart() &&
          period->GetTlDuration() > 0)
      {
        m_currentPeriod = period.get();
      }
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
      m_locationUrl = URL::Join(URL::GetBaseDomain(base_url_), locationUrl.data());
    else
      m_locationUrl = locationUrl;
  }

  // Parse <MPD> <UTCTiming> tags
  if (!m_clockOffset.has_value())
    m_clockOffset = ResolveUTCTiming(nodeMPD);

  if (m_isLive)
  {
    // If TSB is not set but availabilityStartTime, use the last one as TSB
    // since all segments from availabilityStartTime are available
    if (m_timeShiftBufferDepth == 0 && available_time_ > 0)
    {
      const uint64_t now = stream_start_ + *m_clockOffset;
      m_timeShiftBufferDepth = now - available_time_;
    }

    // Templated representations can have very large TSB
    // so limit it to avoid excessive memory consumption
    m_tsbLimited = 14400000; // Default 4 hours

    auto& manifestCfg = CSrvBroker::GetKodiProps().GetManifestConfig();
    if (manifestCfg.timeShiftBufferLimit.has_value())
      m_tsbLimited = *manifestCfg.timeShiftBufferLimit * 1000;

    if (m_timeShiftBufferDepth < m_tsbLimited)
      m_tsbLimited = m_timeShiftBufferDepth;
  }

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

  UpdateTotalTime();

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
    available_time_ =
        static_cast<uint64_t>(XML::ParseDate(availabilityStartTimeStr.c_str()) * 1000);

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

void adaptive::CDashTree::ParseTagPeriod(pugi::xml_node nodePeriod, const std::string& mpdUrl)
{
  std::unique_ptr<CPeriod> period = CPeriod::MakeUniquePtr();

  period->SetIndex(m_periodIndex++);

  // Parse <Period> attributes

  period->SetId(XML::GetAttrib(nodePeriod, "id"));

  std::string_view start = XML::GetAttrib(nodePeriod, "start");
  if (!start.empty())
    period->SetStart(static_cast<uint64_t>(XML::ParseDuration(start) * 1000) + available_time_);
  else if (m_isLive)
  {
    // "start" attribute on first period is mandatory on dynamic manifest type to help mapping periods on updates
    // on subsequent periods it can be determined
    if (m_periods.empty())
    {
      LOG::LogF(LOGWARNING, "Period ID \"%s\" has no \"start\" attribute, assumed 0.", period->GetId().c_str());
      period->SetStart(available_time_);
    }
    else
    {
      auto& lastPeriod = m_periods.back();
      uint64_t pStartMs = lastPeriod->GetStart() + (lastPeriod->GetDuration() * 1000 / lastPeriod->GetTimescale());
      period->SetStart(pStartMs);
    }
  }

  period->SetDuration(
      static_cast<uint64_t>(XML::ParseDuration(XML::GetAttrib(nodePeriod, "duration")) * 1000));

  if (period->GetDuration() == 0)
  {
    // If no duration, try look at next Period to determine it
    //! @todo: if next period has no start attribute, try get duration from a timeline
    pugi::xml_node nodeNextPeriod = nodePeriod.next_sibling();
    if (nodeNextPeriod)
    {
      std::string_view nextStartStr = XML::GetAttrib(nodeNextPeriod, "start");
      uint64_t nextStart{0};

      if (!nextStartStr.empty())
      {
        nextStart =
            static_cast<uint64_t>(XML::ParseDuration(nextStartStr) * 1000) + available_time_;
      }

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
      period->SetBaseUrl(URL::Join(mpdUrl, baseUrl));
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
      if (value == "forced-subtitle")
        adpSet->SetIsForced(true);
      else if (value == "main")
        adpSet->SetIsDefault(true);
      else if (value == "alternate" || value == "commentary")
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
    else if (schemeIdUri == "urn:tva:metadata:cs:AudioPurposeCS:2007")
    {
      if (value == "1") // Visually impaired
        adpSet->SetIsImpaired(true);
      else if (value == "2") // Hearing impaired
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
              adpSet->GetId().c_str());
    return;
  }

  adpSet->SetGroup(XML::GetAttrib(nodeAdp, "group"));

  // Language code format IETF RFC 5646 (BCP-47)
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
    else if (schemeIdUri == "urn:mpeg:mpegB:cicp:TransferCharacteristics")
    {
      if (value == "16")
        adpSet->SetColorTRC(ColorTRC::SMPTE2084);
      else if (value == "18")
        adpSet->SetColorTRC(ColorTRC::ARIB_STD_B67);
    }
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
    auto segTemplate = period->GetSegmentTemplate().value_or(CSegmentTemplate());

    if (nodeSegTpl)
      ParseSegmentTemplate(nodeSegTpl, segTemplate);

    adpSet->SetSegmentTemplate(segTemplate);
  }

  // Parse <SegmentList> tag
  xml_node nodeSeglist = nodeAdp.child("SegmentList");
  if (nodeSeglist)
  {
    auto segList = adpSet->GetSegmentList().value_or(CSegmentList());

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
              adpSet->GetId().c_str());
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
              repr->GetId().c_str());
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
  std::string codecPrivateData;
  if (XML::QueryAttrib(nodeRepr, "codecPrivateData", codecPrivateData))
  {
    repr->SetCodecPrivateData(STRING::HexToBytes(codecPrivateData));
  }

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
      repr->SetBaseUrl(URL::Join(adpSet->GetBaseUrl(), baseUrl.data()));
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
    auto segTemplate = adpSet->GetSegmentTemplate().value_or(CSegmentTemplate());

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
    auto segList = adpSet->GetSegmentList().value_or(CSegmentList());

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
      if (ParseRangeRFC(XML::GetAttrib(node, "mediaRange").data(), rangeStart, rangeEnd))
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
    ParseTagContentProtection(nodeRepr, repr->ProtectionSchemes());
  }

  // Store the protection data
  if (adpSet->HasProtectionSchemes() || repr->HasProtectionSchemes())
  {
    GetProtectionData(adpSet->ProtectionSchemes(), repr->ProtectionSchemes(), *repr.get());
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
      // const bool hasPTO = segTemplate->HasPresTimeOffset();

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
            seg.startPTS_ = time + periodStartScaled;
            seg.m_endPts = seg.startPTS_ + tlElem.duration;

            if (hasMediaNumber)
              seg.m_number = segNumber++;

            seg.m_time = time;

            repr->Timeline().Add(seg);

            time += tlElem.duration;
          } while (repeat-- > 0);
        }
      }
      else // Generate segments by using template
      {
        const uint64_t nowMs = stream_start_ + *m_clockOffset;
        uint64_t periodDurMs = period->GetDuration() * 1000 / period->GetTimescale();
        if (periodDurMs == 0)
          periodDurMs = m_mediaPresDuration;

        // If signalled limit number of segments to the end segment number
        uint64_t segNumberEnd = SEGMENT_NO_NUMBER;
        if (segTemplate->HasEndNumber())
          segNumberEnd = segTemplate->GetEndNumber();
        else if (repr->HasSegmentEndNr())
          segNumberEnd = repr->GetSegmentEndNr();

        GenerateTemplatedSegments(*segTemplate, periodStartMs, periodDurMs, segNumberEnd,
                                  repr->Timeline(), nowMs);
      }

      repr->SetTimescale(segTimescale);
    }
  }

  repr->SetScaling();

  // Update period timeline duration
  if (repr->HasSegmentBase())
  {
    // Use this as an estimate (also for multi-periods) it will then be updated using SIDX parsing
    period->SetTlDuration(period->GetDuration());
  }
  if (!repr->HasSegmentBase() && (adpSet->GetStreamType() == StreamType::VIDEO ||
      adpSet->GetStreamType() == StreamType::AUDIO))
  {
    if (repr->GetTimescale() == 0)
    {
      LOG::LogF(LOGERROR, "Cannot calculate timeline duration, missing timescale attribute");
    }
    else
    {
      const uint64_t tlDuration =
          repr->Timeline().GetDuration() * period->GetTimescale() / repr->GetTimescale();
      period->SetTlDuration(tlDuration);
    }
  }

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
    const std::string schemeIdUri{XML::GetAttrib(nodeCP, "schemeIdUri")};

    ProtectionScheme protScheme;
    protScheme.idUri = STRING::ToLower(schemeIdUri);
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
        DRM::PRHeaderParser parser;
        if (parser.Parse(node.child_value()))
        {
          protScheme.kid = STRING::ToHexadecimal(parser.GetKID());
          protScheme.pssh =
              BASE64::Encode(DRM::PSSH::Make(DRM::ID_PLAYREADY, {}, parser.GetInitData()));
          protScheme.licenseUrl = parser.GetLicenseURL();

          auto encryptionType = parser.GetEncryption();
          if (encryptionType == DRM::PRHeaderParser::EncryptionType::AESCTR)
            protScheme.value = "cenc";
          else if (encryptionType == DRM::PRHeaderParser::EncryptionType::AESCBC)
            protScheme.value = "cbcs";
        }
      }
    }

    // There are no constraints on the Kid format, it is recommended to be as UUID but not mandatory
    STRING::ReplaceAll(protScheme.kid, "-", "");
    protScheme.kid = STRING::ToLower(protScheme.kid);

    protSchemes.emplace_back(protScheme);
  }
}

void adaptive::CDashTree::GetProtectionData(
    const std::vector<PLAYLIST::ProtectionScheme>& adpProtSchemes,
    std::vector<PLAYLIST::ProtectionScheme>& reprProtSchemes,
    PLAYLIST::CRepresentation& repr)
{
  reprProtSchemes.insert(reprProtSchemes.end(), adpProtSchemes.begin(), adpProtSchemes.end());

  CryptoMode cryptoMode = CryptoMode::AES_CTR; // default cenc
  bool isCencSchemeOnly{false}; // ContentProtection with cenc only, no DRM scheme provided

  // Find the encryption scheme
  for (const ProtectionScheme& protScheme : reprProtSchemes)
  {
    if (protScheme.idUri == "urn:mpeg:dash:mp4protection:2011")
    {
      std::string_view encryptionScheme = protScheme.value;
      if (encryptionScheme == "cenc")
        cryptoMode = CryptoMode::AES_CTR;
      else if (encryptionScheme == "cbcs")
        cryptoMode = CryptoMode::AES_CBC;
      else if (!encryptionScheme.empty())
        LOG::LogF(LOGERROR, "Unsupported encryption scheme: %s", encryptionScheme.data());

      isCencSchemeOnly = reprProtSchemes.size() == 1;
      break;
    }
  }

  // Find the default KeyId, if there are multiple they must be all the same
  std::set<std::string> keyIds;
  for (const ProtectionScheme& protScheme : reprProtSchemes)
  {
    if (!protScheme.kid.empty())
      keyIds.emplace(protScheme.kid);
  }

  if (keyIds.size() > 1)
  {
    LOG::LogF(LOGERROR, "Conflicting KeyId's on ContentProtection tags");
    return;
  }

  for (const ProtectionScheme& protScheme : reprProtSchemes)
  {
    std::string_view keySystem = DRM::UrnToKeySystem(protScheme.idUri);

    if (!keySystem.empty() || isCencSchemeOnly)
    {
      DRM::DRMInfo drmInfo;
      drmInfo.keySystem = keySystem;

      std::vector<uint8_t> initData = BASE64::Decode(protScheme.pssh);
      if (!keySystem.empty() && !initData.empty() && !DRM::IsValidPsshHeader(initData))
      {
        // Fix missing PSSH box (e.g. Amazon video service dont provide a CENC PSSH on ContentProtection)
        initData = DRM::PSSH::Make(DRM::KeySystemToUUID(keySystem), {}, initData);
      }

      drmInfo.initData = initData;
      drmInfo.licenseServerUri = protScheme.licenseUrl;
      drmInfo.cryptoMode = cryptoMode;
      if (!keyIds.empty())
        drmInfo.defaultKid = *keyIds.begin();

      repr.AddDrmInfo(drmInfo);
    }
  }
}

std::optional<bool> adaptive::CDashTree::ParseTagContentProtectionSecDec(pugi::xml_node nodeParent)
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
        if (robustnessLevel == "HW_SECURE_CODECS_NOT_ALLOWED")
          return false;
        else if (robustnessLevel == "HW_SECURE_CODECS_REQUIRED")
          return true;
      }
    }
  }
  return std::nullopt;
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

int64_t adaptive::CDashTree::ResolveUTCTiming(pugi::xml_node node)
{
  std::vector<std::pair<std::string, std::string>> utcTimings;

  // Custom UTC timing
  auto& manifestCfg = CSrvBroker::GetKodiProps().GetManifestConfig();
  if (manifestCfg.dashUTCTiming.has_value())
    utcTimings.emplace_back(*manifestCfg.dashUTCTiming);

  // Parse <UTCTiming> child tags
  for (xml_node nodeUTC : node.children("UTCTiming"))
  {
    utcTimings.emplace_back(XML::GetAttrib(nodeUTC, "schemeIdUri"), XML::GetAttrib(nodeUTC, "value"));
  }

  std::optional<int64_t> tsMs;

  // Elements are in order of preference so the first have the highest priority
  for (auto& [scheme, value] : utcTimings)
  {
    if (scheme == "urn:mpeg:dash:utc:http-head:2012" ||
        scheme == "urn:mpeg:dash:utc:http-head:2014")
    {
      const uint64_t ts =
          UTILS::ConvertDate2822ToTs(RequestUTCTiming(value, CURL::RequestType::HEAD));
      if (ts == 0)
      {
        LOG::LogF(LOGERROR, "UTCTiming conversion failed from scheme \"%s\"", scheme.c_str());
        continue;
      }
      tsMs = static_cast<int64_t>(ts * 1000);
      break;
    }
    else if (scheme == "urn:mpeg:dash:utc:http-xsdate:2014" ||
             scheme == "urn:mpeg:dash:utc:http-iso:2014" ||
             scheme == "urn:mpeg:dash:utc:http-xsdate:2012" ||
             scheme == "urn:mpeg:dash:utc:http-iso:2012")
    {
      const double ts = XML::ParseDate(RequestUTCTiming(value, CURL::RequestType::AUTO).c_str(), 0);
      if (ts == 0)
      {
        LOG::LogF(LOGERROR, "UTCTiming conversion failed from scheme \"%s\"", scheme.c_str());
        continue;
      }
      tsMs = static_cast<int64_t>(ts * 1000);
      break;
    }
    else if (scheme == "urn:mpeg:dash:utc:direct:2014" ||
             scheme == "urn:mpeg:dash:utc:direct:2012")
    {
      const double ts = XML::ParseDate(value.c_str(), 0);
      if (ts == 0)
      {
        LOG::LogF(LOGERROR, "A problem occurred in the UTCTiming scheme \"%s\" parsing", scheme.c_str());
        continue;
      }
      tsMs = static_cast<int64_t>(ts * 1000);
      break;
    }
    else if (scheme == "urn:mpeg:dash:utc:http-ntp:2014" ||
             scheme == "urn:mpeg:dash:utc:ntp:2014" ||
             scheme == "urn:mpeg:dash:utc:sntp:2014")
    {
      LOG::Log(LOGDEBUG, "NTP UTCTiming scheme \"%s\" not supported", scheme.c_str());
    }
    else
      LOG::Log(LOGDEBUG, "UTCTiming scheme \"%s\" not supported", scheme.c_str());
  }

  int64_t offset{0};

  if (tsMs.has_value())
  {
    offset = *tsMs - GetTimestamp();
    LOG::Log(LOGDEBUG, "UTCTiming clock offset %lli ms", offset);
  }
  else if (!utcTimings.empty())
    LOG::Log(LOGWARNING, "Unable to get UTCTiming, playback problems may occur");

  return offset;
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
        //  the only difference is in the ContentProtection kid/pssh and the base url,
        //  in order not to show several identical audio tracks in the Kodi GUI, we must merge adaptation sets
        // CompareSwitchingId:
        //  Some services can provide switchable video adp sets, these could havedifferent codecs, and could be
        //  used to split HD resolutions from SD, so to allow Chooser's to autoselect the video quality
        //  we need to merge them all
        // CODEC NOTE: since we cannot know in advance the supported video codecs by the hardware in use
        //  we cannot merge adp sets with different codecs otherwise playback will not work
        if (adpSet->CompareSwitchingId(nextAdpSet) || adpSet->IsMergeable(nextAdpSet))
        {
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

bool adaptive::CDashTree::DownloadManifestUpd(const std::string& url,
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
  if (m_locationUrl.empty())
  {
    manifestUrl = manifest_url_;
    if (!manifestParams.empty())
      manifestUrl = URL::RemoveParameters(manifestUrl);
  }
  else
    manifestUrl = m_locationUrl;

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

  OverrideStreamsMediaFlags(updateTree->m_periods);

  const uint64_t liveEdgeMs = GetTimestamp() - m_timeShiftBufferDepth + *m_clockOffset;

  // Update local members for the next manifest update
  m_manifestRespHeaders = resp.headers;
  m_locationUrl = updateTree->m_locationUrl;
  m_mediaPresDuration = updateTree->m_mediaPresDuration;
  //! @todo: Live (dynamic) to VOD (static) untested

  std::set<uint64_t> updatedPeriodStarts;

  for (size_t index{0}; index < updateTree->m_periods.size(); index++)
  {
    auto& updPeriod = updateTree->m_periods[index];

    updatedPeriodStarts.insert(updPeriod->GetStart());

    // The periods mapping between local MPD data and the MPD update is done by using period "start" attribute,
    // since the "start" attribute is mandatory and must not change over MPD updates

    auto itPeriod = std::find_if(
        m_periods.begin(), m_periods.end(), [&updPeriod](const std::unique_ptr<CPeriod>& item)
        { return item->GetStart() == updPeriod->GetStart(); });

    CPeriod* period{nullptr};

    if (itPeriod == m_periods.end()) // Period dont exist
    {
      LOG::LogF(LOGDEBUG, "Inserting new Period (id=%s, start=%llu)", updPeriod->GetId().c_str(),
                updPeriod->GetStart());

      updPeriod->SetIndex(m_periodIndex++);
      m_periods.push_back(std::move(updPeriod));
      continue;
    }
    else // Update existing one
    {
      period = (*itPeriod).get();

      // Update period data that may be added or changed
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

            if (!repr->GetSegmentTemplate()->HasTimeline() || repr->Timeline().IsEmpty())
            {
              LOG::LogF(LOGDEBUG, "MPD update - No timeline (repr. id \"%s\", period id \"%s\")",
                        repr->GetId().c_str(), period->GetId().c_str());
              continue;
            }

            if (!repr->current_segment_.has_value()) // Representation not used for playback yet
            {
              repr->Timeline().Swap(updRepr->Timeline());

              LOG::LogF(LOGDEBUG, "MPD update - Done (repr. id \"%s\", period id \"%s\")",
                        updRepr->GetId().c_str(), period->GetId().c_str());
              continue;
            }

            if (repr->Timeline().GetInitialSize() == updRepr->Timeline().GetSize() &&
                repr->Timeline().Get(0)->startPTS_ == updRepr->Timeline().Get(0)->startPTS_)
            {
              LOG::LogF(LOGDEBUG,
                        "MPD update - No new segments (repr. id \"%s\", period id \"%s\")",
                        repr->GetId().c_str(), period->GetId().c_str());
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
                          repr->GetId().c_str(), period->GetId().c_str());
                break;
              }
            }

            if (!foundSeg)
            {
              LOG::LogF(LOGDEBUG,
                        "MPD update - No segment found (repr. id \"%s\", period id \"%s\")",
                        repr->GetId().c_str(), period->GetId().c_str());
            }
            else
            {
              repr->Timeline().Swap(updRepr->Timeline());
              repr->current_segment_ = *foundSeg;

              // Update period duration
              if (adpSet->GetStreamType() == StreamType::VIDEO ||
                  adpSet->GetStreamType() == StreamType::AUDIO)
              {
                const uint64_t tlDuration = updRepr->Timeline().GetDuration() *
                                            period->GetTimescale() / updRepr->GetTimescale();
                period->SetTlDuration(tlDuration);
              }

              LOG::LogF(LOGDEBUG, "MPD update - Done (repr. id \"%s\", period id \"%s\")",
                        updRepr->GetId().c_str(), period->GetId().c_str());
            }
          }
        }
      }
    }
  }

  // Delete removed periods
  auto it = std::remove_if(
      m_periods.begin(), m_periods.end(),
      [this, &updatedPeriodStarts, &liveEdgeMs](const std::unique_ptr<CPeriod>& period)
      {
        // Do not try to delete the current period, its necessary for the transition to the new period
        // this can happens if you keep video in pause for long time
        if (period.get() == m_currentPeriod)
          return false;

        if (updatedPeriodStarts.find(period->GetStart()) == updatedPeriodStarts.end())
        {
          LOG::Log(LOGDEBUG, "Deleted period (ID: \"%s\", start: %llu)", period->GetId().c_str(),
                   period->GetStart());
          return true;
        }
        return false;
      });
  m_periods.erase(it, m_periods.end());

  UpdateTotalTime();
}

void adaptive::CDashTree::GenerateTemplatedSegments(PLAYLIST::CSegmentTemplate& segTemplate,
                                                    const uint64_t periodStartMs,
                                                    const uint64_t periodDurMs,
                                                    const uint64_t segNumberEnd,
                                                    PLAYLIST::CSegContainer& timeline,
                                                    const uint64_t nowMs)
{
  const uint32_t segTimescale = segTemplate.GetTimescale();
  const uint64_t segDuration = static_cast<uint64_t>(segTemplate.GetDuration());
  const uint64_t periodEndMs = periodDurMs == 0 ? NO_PTS_VALUE : periodStartMs + periodDurMs;
  uint64_t segNumber = segTemplate.GetStartNumber();
  uint64_t time = periodStartMs * segTimescale / 1000;

  uint64_t wndStart;
  uint64_t wndEnd;
  if (m_tsbLimited > 0) // Calculate TSB window
  {
    wndStart = nowMs - m_tsbLimited;
    wndEnd = wndStart + m_tsbLimited;

    if (wndEnd > periodStartMs)
    {
      if (wndStart < periodStartMs)
        wndStart = periodStartMs;

      // The duration is known
      if (periodEndMs != NO_PTS_VALUE)
      {
        if (periodEndMs < wndStart)
          return; // This period is outside the TSB

        // Assume window available until the end
        if (nowMs > periodEndMs && wndEnd > periodEndMs)
          wndEnd = periodEndMs;
      }

      if (!timeline.IsEmpty())
      {
        // A timeline already exists continue from it (assumed as always updated)
        auto lastSeg = timeline.GetBack();
        time = lastSeg->m_endPts;
        segNumber = lastSeg->m_number + 1;
      }
      else
      {
        // Align segment number and time to TSB start
        segNumber += (wndStart - periodStartMs) / (segDuration * 1000 / segTimescale);
        time += (segNumber - segTemplate.GetStartNumber()) * segDuration;
      }
    }
  }
  else // No TSB, use period duration
  {
    wndStart = periodStartMs;
    wndEnd = periodEndMs;
  }

  const uint64_t timeEnd = wndEnd * segTimescale / 1000;

  // Create segments within time window,
  // a segment that begins within the period and partially
  // extends beyond the period boundaries must still be generated
  //! @todo: currently when a segment extends beyond the period boundaries
  //! it will exist also on next period, this will cause duplicate playback of the content (you can see also twice downloads)
  //! in theory this should be fixed in the sample reader, where the sample packets of a segment
  //! (at the beginning or end) must be filtered based on the duration of the period.
  //! In other words, finish playing a segment early by skipping the samples that fall outside the period,
  //! while in the next period, start playing the segment from a sample with a specific PTS,
  //! thus skipping part of the initial samples.
  while (time < timeEnd && segNumber <= segNumberEnd)
  {
    CSegment seg;
    seg.startPTS_ = time;
    seg.m_endPts = time + segDuration;
    seg.m_number = segNumber;
    seg.m_time = time;

    timeline.Add(seg);

    segNumber++;
    time = seg.m_endPts;
  }
}

void adaptive::CDashTree::UpdateTotalTime()
{
  uint64_t totalDurMs = m_mediaPresDuration;
  if (totalDurMs == 0)
  {
    totalDurMs =
        std::accumulate(m_periods.begin(), m_periods.end(), uint64_t{0},
                        [](uint64_t sum, const std::unique_ptr<CPeriod>& period)
                        { return sum + period->GetTlDuration() * 1000 / period->GetTimescale(); });
  }

  m_totalTime = totalDurMs;
}

void adaptive::CDashTree::InsertLiveSegment(PLAYLIST::CPeriod* currPeriod,
                                            PLAYLIST::CAdaptationSet* currAdpSet,
                                            PLAYLIST::CRepresentation* currRepr)
{
  // This code will keep updated the timeline on all representations
  // (across all periods) with current now time (TSB included),
  // exclusive case of representations having SegmentTemplate without defined timeline

  //! @todo: The removal of expired periods is done by OnRequestSegments method
  //! but you need to take in account that periods that fall outside a limited/shrinked TSB
  //! will not be deleted (they have an empty timeline)
  //! nothing changes for playback but the GUI will report a total chapters
  //! greater than those actually playable, this should be improved in future
  //! by removing also periods outside a limited TSB

  const uint64_t nowMs = GetTimestamp() + *m_clockOffset;
  const uint64_t liveEdgeMs = nowMs - m_tsbLimited; // live edge in ms

  for (auto& period : m_periods)
  {
    const uint64_t periodStartMs = period->GetStart() == NO_VALUE ? 0 : period->GetStart(); 
    const uint64_t periodDurMs = period->GetDuration() * 1000 / period->GetTimescale();
    const bool isPeriodTSB = period->IsInRange(liveEdgeMs);

    for (auto& adpSet : period->GetAdaptationSets())
    {
      for (auto& rep : adpSet->GetRepresentations())
      {
        // Generate segments to templated representation with no defined timeline only
        if (!rep->HasSegmentTemplate() || rep->GetSegmentTemplate()->HasTimeline())
          continue;

        // Update the TSB by extending it, and by removing segments that fall outside the TSB
        if (isPeriodTSB)
        {
          const uint64_t liveEdgeScaled =
              liveEdgeMs * rep->GetSegmentTemplate()->GetTimescale() / 1000;

          rep->Timeline().PruneToTime(liveEdgeScaled);

          // If signalled limit number of segments to the end segment number
          uint64_t segNumberEnd = SEGMENT_NO_NUMBER;
          if (rep->GetSegmentTemplate()->HasEndNumber())
            segNumberEnd = rep->GetSegmentTemplate()->GetEndNumber();
          else if (rep->HasSegmentEndNr())
            segNumberEnd = rep->GetSegmentEndNr();

          GenerateTemplatedSegments(*rep->GetSegmentTemplate(), periodStartMs, periodDurMs,
                                    segNumberEnd, rep->Timeline(), nowMs);
        }
        else
        {
          // The period is outside the TSB so delete all segments,
          // this is the case of video resume from a long pause
          rep->Timeline().Clear();
          rep->current_segment_.reset();
        }

        // Update period timeline duration
        if (!rep->HasSegmentBase() && (adpSet->GetStreamType() == StreamType::VIDEO ||
            adpSet->GetStreamType() == StreamType::AUDIO))
        {
          if (rep->GetTimescale() == 0)
          {
            LOG::LogF(LOGERROR, "Cannot calculate timeline duration, missing timescale attribute");
          }
          else
          {
            const uint64_t tlDuration =
                rep->Timeline().GetDuration() * period->GetTimescale() / rep->GetTimescale();
            period->SetTlDuration(tlDuration);
          }
        }
      }
    }
  }

  UpdateTotalTime();
}
