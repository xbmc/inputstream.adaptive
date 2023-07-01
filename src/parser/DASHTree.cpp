/*
 *  Copyright (C) 2023 Team Kodi
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
#include "../utils/XMLUtils.h"
#include "../utils/log.h"
#include "PRProtectionParser.h"
#include "kodi/tools/StringUtils.h"
#include "pugixml.hpp"

#include <algorithm> // max
#include <cmath>
#include <cstdio> // sscanf
#include <numeric> // accumulate
#include <string>
#include <thread>

using namespace pugi;
using namespace kodi::tools;
using namespace PLAYLIST;
using namespace UTILS;

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

  return ContainerType::MP4;
}

std::string DetectCodecFromMimeType(std::string_view mimeType)
{
  if (mimeType == "text/vtt")
    return "wvtt";
  if (mimeType == "application/ttml+xml")
    return "ttml";

  return "";
}

} // unnamed namespace


adaptive::CDashTree::CDashTree(const CDashTree& left) : AdaptiveTree(left)
{
}

bool adaptive::CDashTree::Open(std::string_view url,
                               const std::map<std::string, std::string>& headers,
                               const std::string& data)
{
  SaveManifest("", data, url);

  m_manifestRespHeaders = headers;
  manifest_url_ = url;
  base_url_ = URL::RemoveParameters(url.data());

  if (!ParseManifest(data))
    return false;

  if (m_periods.empty())
  {
    LOG::Log(LOGWARNING, "No periods in the manifest");
    return false;
  }

  m_currentPeriod = m_periods[0].get();

  SortTree();

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

  stream_start_ = GetTimestamp();

  xml_node nodeMPD = doc.child("MPD");
  if (!nodeMPD)
  {
    LOG::LogF(LOGERROR, "Failed to get manifest <MPD> tag element.");
    return false;
  }

  // Parse <MPD> tag attributes
  ParseTagMPDAttribs(nodeMPD);

  // Parse <MPD> <Location> tag
  std::string_view locationText = nodeMPD.child("Location").child_value();
  if (!locationText.empty() && URL::IsValidUrl(locationText.data()))
    location_ = locationText;

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

  // Cleanup periods
  bool hasTotalTimeSecs = m_totalTimeSecs > 0;

  for (auto itPeriod = m_periods.begin(); itPeriod != m_periods.end();)
  {
    auto& period = *itPeriod;
    if (hasTotalTimeSecs && period->GetDuration() == 0)
    {
      auto nextPeriod = itPeriod + 1;
      if (nextPeriod == m_periods.end())
      {
        period->SetDuration(
            ((m_totalTimeSecs * 1000 - period->GetStart()) * period->GetTimescale()) / 1000);
      }
      else
      {
        period->SetDuration(
            (((*nextPeriod)->GetStart() - period->GetStart()) * period->GetTimescale()) / 1000);
      }
    }
    if (period->GetAdaptationSets().empty())
    {
      if (hasTotalTimeSecs)
        m_totalTimeSecs -= period->GetDuration() / period->GetTimescale();

      itPeriod = m_periods.erase(itPeriod);
    }
    else
    {
      if (!hasTotalTimeSecs)
        m_totalTimeSecs += period->GetDuration() / period->GetTimescale();

      itPeriod++;
    }
  }

  return true;
}

void adaptive::CDashTree::ParseTagMPDAttribs(pugi::xml_node nodeMPD)
{
  double mediaPresDuration =
      XML::ParseDuration(XML::GetAttrib(nodeMPD, "mediaPresentationDuration"));

  m_isLive = XML::GetAttrib(nodeMPD, "type") == "dynamic";

  double timeShiftBufferDepth{0};
  std::string timeShiftBufferDepthStr;
  if (XML::QueryAttrib(nodeMPD, "timeShiftBufferDepth", timeShiftBufferDepthStr))
  {
    timeShiftBufferDepth = XML::ParseDuration(timeShiftBufferDepthStr);
    m_isLive = true;
  }

  std::string availabilityStartTimeStr;
  if (XML::QueryAttrib(nodeMPD, "availabilityStartTime", availabilityStartTimeStr))
    available_time_ = XML::ParseDate(availabilityStartTimeStr);

  std::string suggestedPresentationDelayStr;
  if (XML::QueryAttrib(nodeMPD, "suggestedPresentationDelay", suggestedPresentationDelayStr))
    m_liveDelay = static_cast<uint64_t>(XML::ParseDuration(suggestedPresentationDelayStr));

  std::string minimumUpdatePeriodStr;
  if (XML::QueryAttrib(nodeMPD, "minimumUpdatePeriod", minimumUpdatePeriodStr))
  {
    double duration = XML::ParseDuration(minimumUpdatePeriodStr) * 1000;
    m_updateInterval = static_cast<uint64_t>(duration);
  }

  if (mediaPresDuration == 0)
    m_totalTimeSecs = static_cast<uint64_t>(timeShiftBufferDepth);
  else
    m_totalTimeSecs = static_cast<uint64_t>(mediaPresDuration);
}

void adaptive::CDashTree::ParseTagPeriod(pugi::xml_node nodePeriod, std::string_view mpdUrl)
{
  std::unique_ptr<CPeriod> period = CPeriod::MakeUniquePtr();

  period->SetSequence(m_periodCurrentSeq++);

  // Parse <Period> attributes
  period->SetId(XML::GetAttrib(nodePeriod, "id"));
  period->SetStart(
      static_cast<uint64_t>(XML::ParseDuration(XML::GetAttrib(nodePeriod, "start")) * 1000));
  period->SetDuration(
      static_cast<uint64_t>(XML::ParseDuration(XML::GetAttrib(nodePeriod, "duration")) * 1000));

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

    ParseSegmentTemplate(nodeSegTpl, &segTemplate);

    // Parse <SegmentTimeline> child
    xml_node nodeSegTL = nodeSegTpl.child("SegmentTimeline");
    if (nodeSegTL)
    {
      uint64_t startPts = ParseTagSegmentTimeline(nodeSegTL, period->SegmentTimelineDuration(),
                                                  segTemplate.GetTimescale());

      period->SetStartPTS(startPts);

      if (period->GetDuration() == 0 && segTemplate.GetTimescale() > 0)
      {
        // Calculate total duration of segments
        auto& segTLData = period->SegmentTimelineDuration().GetData();
        uint32_t sum = std::accumulate(segTLData.begin(), segTLData.end(), 0);
        period->SetDuration(sum);
        period->SetTimescale(segTemplate.GetTimescale());
      }
    }

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
    {
      segList.SetDuration(duration);
      period->SetDuration(duration);
    }

    uint32_t timescale;
    if (XML::QueryAttrib(nodeSeglist, "timescale", timescale))
    {
      segList.SetTimescale(timescale);
      period->SetTimescale(timescale);
    }

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
  // this helps to avoid merging adpSets (done with AdaptiveTree::SortTree) for some limit cases
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
      if (value == "caption")
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
    adpSet->SetIsOriginal(isDefault == "true");

  // Parse <AudioChannelConfiguration> child tag
  xml_node nodeAudioCh = nodeAdp.child("AudioChannelConfiguration");
  if (nodeAudioCh)
    adpSet->SetAudioChannels(ParseAudioChannelConfig(nodeAudioCh));

  // Parse <SupplementalProperty> child tag
  xml_node nodeSupplProp = nodeAdp.child("SupplementalProperty");
  if (nodeSupplProp)
  {
    std::string_view schemeIdUri = XML::GetAttrib(nodeSupplProp, "schemeIdUri");
    std::string_view value = XML::GetAttrib(nodeSupplProp, "value");

    if (schemeIdUri == "urn:mpeg:dash:adaptation-set-switching:2016")
      adpSet->AddSwitchingIds(value);
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

  // Parse <SegmentDurations> tag
  // No dash spec, looks like a custom Amazon video service implementation
  xml_node nodeSegDur = nodeAdp.child("SegmentDurations");
  if (nodeSegDur)
  {
    period->SetTimescale(XML::GetAttribUint32(nodeSegDur, "timescale", 1000));

    // Parse <S> tags - e.g. <S d="90000"/>
    // add all duration values as timeline segments
    for (xml_node node : nodeSegDur.children("S"))
    {
      adpSet->SegmentTimelineDuration().GetData().emplace_back(XML::GetAttribUint32(node, "d"));
    }
  }

  // Parse <SegmentTemplate> tag
  xml_node nodeSegTpl = nodeAdp.child("SegmentTemplate");
  if (nodeSegTpl)
  {
    CSegmentTemplate segTemplate;
    if (period->HasSegmentTemplate())
      segTemplate = *period->GetSegmentTemplate();

    ParseSegmentTemplate(nodeSegTpl, &segTemplate);

    // Parse <SegmentTemplate> <SegmentTimeline> child
    xml_node nodeSegTL = nodeSegTpl.child("SegmentTimeline");
    if (nodeSegTL)
    {
      uint64_t startPts = ParseTagSegmentTimeline(nodeSegTL, adpSet->SegmentTimelineDuration(),
                                                  segTemplate.GetTimescale());

      adpSet->SetStartPTS(startPts);

      if (period->GetDuration() == 0 && segTemplate.GetTimescale() > 0)
      {
        // Calculate total duration of segments
        auto& segTLData = adpSet->SegmentTimelineDuration().GetData();
        uint32_t sum = std::accumulate(segTLData.begin(), segTLData.end(), 0);
        period->SetDuration(sum);
        period->SetTimescale(segTemplate.GetTimescale());
      }
    }
    adpSet->SetSegmentTemplate(segTemplate);
  }
  else if (period->HasSegmentTemplate())
    adpSet->SetSegmentTemplate(*period->GetSegmentTemplate());

  // Parse <SegmentList> tag
  xml_node nodeSeglist = nodeAdp.child("SegmentList");
  if (nodeSeglist)
  {
    CSegmentList segList;
    if (adpSet->HasSegmentList())
      segList = *adpSet->GetSegmentList();

    uint64_t duration;
    if (XML::QueryAttrib(nodeSeglist, "duration", duration))
    {
      segList.SetDuration(duration);
      period->SetDuration(duration);
    }

    uint32_t timescale;
    if (XML::QueryAttrib(nodeSeglist, "timescale", timescale))
    {
      segList.SetTimescale(timescale);
      period->SetTimescale(timescale);
    }

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
      uint64_t startPts = ParseTagSegmentTimeline(nodeSegTL, adpSet->SegmentTimelineDuration(),
                                                  segList.GetTimescale());

      adpSet->SetStartPTS(startPts);
    }
  }

  // Parse <Representation> child tags
  bool hasReprURN{false}; // True if representations has URNs
  for (xml_node node : nodeAdp.children("Representation"))
  {
    ParseTagRepresentation(node, adpSet.get(), period, hasReprURN);
  }

  if (adpSet->GetRepresentations().empty())
  {
    LOG::LogF(LOGWARNING, "Skipped AdaptationSet with id: \"%s\", has no representations.",
              adpSet->GetId().data());
    return;
  }

  // Parse <ContentProtection> child tags
  if (nodeAdp.child("ContentProtection"))
  {
    period->SetEncryptionState(EncryptionState::ENCRYPTED);
    uint16_t currentPsshSetPos{PSSHSET_POS_DEFAULT};
    std::string currentPssh{PSSH_FROM_FILE};
    std::string currentDefaultKID;
    bool isSecureDecoderNeeded{false};

    if (ParseTagContentProtection(nodeAdp, currentPssh, currentDefaultKID, isSecureDecoderNeeded))
    {
      period->SetEncryptionState(EncryptionState::ENCRYPTED_SUPPORTED);
      currentPsshSetPos = InsertPsshSet(adpSet->GetStreamType(), period, adpSet.get(), currentPssh,
                                        currentDefaultKID);
    }
    if (currentPsshSetPos == PSSHSET_POS_INVALID)
    {
      LOG::LogF(LOGWARNING, "The PSSHSet in the adaptation set id \"%s\" is not valid.",
                adpSet->GetId().data());
    }
    period->SetSecureDecodeNeeded(isSecureDecoderNeeded);

    if (hasReprURN && currentPsshSetPos == PSSHSET_POS_INVALID)
    {
      LOG::LogF(LOGWARNING, "Skipped adaptation set with id: \"%s\", pssh not valid.",
                adpSet->GetId().data());
      return;
    }

    if (currentPsshSetPos != PSSHSET_POS_DEFAULT)
    {
      // Set PSSHSet of AdaptationSet to all representations
      for (auto& rep : adpSet->GetRepresentations())
      {
        rep->m_psshSetPos = currentPsshSetPos;
      }
    }
  }

  period->AddAdaptationSet(adpSet);
}

void adaptive::CDashTree::ParseTagRepresentation(pugi::xml_node nodeRepr,
                                                 PLAYLIST::CAdaptationSet* adpSet,
                                                 PLAYLIST::CPeriod* period,
                                                 bool& hasReprURN)
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

    if (streamType == StreamType::NOTYPE &&
        (repr->ContainsCodec("wvtt") || (repr->ContainsCodec("ttml"))))
    {
      streamType = StreamType::SUBTITLE;
    }
    adpSet->SetStreamType(streamType);
  }

  // Set properties for subtitles types
  if (repr->GetMimeType() != "application/mp4") // Handle text type only, not ISOBMFF format
  {
    if (repr->GetMimeType() == "application/ttml+xml" || repr->GetMimeType() == "text/vtt" ||
        repr->ContainsCodec("wvtt") || repr->ContainsCodec("ttml"))
    {
      if (adpSet->SegmentTimelineDuration().IsEmpty())
        repr->SetIsSubtitleFileStream(true); // Treat as single subtitle file
      else
        repr->SetContainerType(ContainerType::TEXT); // Segmented subtitles
    }
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
  if (nodeSegTpl)
  {
    CSegmentTemplate segTemplate;
    if (adpSet->HasSegmentTemplate())
      segTemplate = *adpSet->GetSegmentTemplate();

    ParseSegmentTemplate(nodeSegTpl, &segTemplate);

    if (segTemplate.HasInitialization())
      repr->SetInitSegment(segTemplate.MakeInitSegment());

    // Parse <SegmentTemplate> <SegmentTimeline> child
    xml_node nodeSegTL = nodeSegTpl.child("SegmentTimeline");
    if (nodeSegTL)
    {
      uint64_t totalTimeSecs{0};
      if (repr->GetDuration() > 0 && repr->GetTimescale() > 0)
        totalTimeSecs = repr->GetDuration() / repr->GetTimescale();

      uint64_t startPts =
          ParseTagSegmentTimeline(nodeSegTL, repr->SegmentTimeline(), segTemplate.GetTimescale(),
                                  totalTimeSecs, &segTemplate);

      repr->nextPts_ = startPts;
    }
    repr->SetTimescale(segTemplate.GetTimescale());
    repr->SetSegmentTemplate(segTemplate);

    repr->SetStartNumber(segTemplate.GetStartNumber());
  }
  else if (adpSet->HasSegmentTemplate())
  {
    CSegmentTemplate segTemplate = *adpSet->GetSegmentTemplate();
    repr->SetTimescale(segTemplate.GetTimescale());
    repr->SetSegmentTemplate(segTemplate);

    repr->SetStartNumber(segTemplate.GetStartNumber());

    if (segTemplate.HasInitialization())
      repr->SetInitSegment(segTemplate.MakeInitSegment());
  }

  // Parse <SegmentList> tag
  xml_node nodeSeglist = nodeRepr.child("SegmentList");
  if (nodeSeglist)
  {
    CSegmentList segList;
    if (repr->HasSegmentList())
      segList = *repr->GetSegmentList();

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

    if (segList.GetStartNumber() > 0)
    {
      repr->SetStartNumber(segList.GetStartNumber());

      segList.SetPresTimeOffset(segList.GetPresTimeOffset() +
                                repr->GetStartNumber() * repr->GetDuration());
    }

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

    if (segList.GetTimescale() > 0 && segList.GetDuration() > 0)
    {
      // Reserve memory to speedup
      repr->SegmentTimeline().GetData().reserve(
          EstimateSegmentsCount(segList.GetDuration(), segList.GetTimescale()));
    }

    // Parse <SegmentList> <SegmentURL> child tags
    size_t index{0};
    uint64_t previousStartPts{0};
    for (xml_node node : nodeSeglist.children("SegmentURL"))
    {
      CSegment seg;

      std::string media;
      if (XML::QueryAttrib(node, "media", media))
      {
        seg.url = media;
        repr->SetHasSegmentsUrl(true);
      }

      bool isTimelineEmpty = repr->SegmentTimeline().IsEmpty();

      uint64_t rangeStart{0};
      uint64_t rangeEnd{0};
      if (ParseRangeRFC(XML::GetAttrib(node, "mediaRange"), rangeStart, rangeEnd))
      {
        seg.range_begin_ = rangeStart;
        seg.range_end_ = rangeEnd;
      }

      uint32_t* tlDuration = adpSet->SegmentTimelineDuration().Get(index);
      uint64_t duration = tlDuration ? *tlDuration : segList.GetDuration();
      if (isTimelineEmpty)
        seg.startPTS_ = segList.GetPresTimeOffset();
      else
      {
        seg.startPTS_ = repr->nextPts_ + duration;
        index++;
      }

      repr->SegmentTimeline().GetData().push_back(seg);
      repr->nextPts_ = seg.startPTS_;
    }

    if (period->GetDuration() == 0 && segList.GetTimescale() > 0)
    {
      // Calculate total duration of segments
      auto& segTLData = adpSet->SegmentTimelineDuration().GetData();
      uint32_t sum = std::accumulate(segTLData.begin(), segTLData.end(), 0);

      if (segList.GetDuration() == 0)
        segList.SetDuration(sum);

      period->SetDuration(sum);
      period->SetTimescale(segList.GetTimescale());
    }

    if (segList.GetDuration() > 0)
      repr->SetDuration(segList.GetDuration());

    if (segList.GetTimescale() > 0)
      repr->SetTimescale(segList.GetTimescale());

    repr->SetSegmentList(segList);
  }

  // Parse <ContentProtection> tags
  if (nodeRepr.child("ContentProtection"))
  {
    period->SetEncryptionState(EncryptionState::ENCRYPTED);
    uint16_t currentPsshSetPos{PSSHSET_POS_DEFAULT};
    std::string currentDefaultKID;
    std::string currentPssh{PSSH_FROM_FILE};
    bool isSecureDecoderNeeded{false};

    if (ParseTagContentProtection(nodeRepr, currentPssh, currentDefaultKID, isSecureDecoderNeeded))
    {
      period->SetEncryptionState(EncryptionState::ENCRYPTED_SUPPORTED);
      currentPsshSetPos =
          InsertPsshSet(adpSet->GetStreamType(), period, adpSet, currentPssh, currentDefaultKID);

      repr->m_psshSetPos = currentPsshSetPos;
      hasReprURN = true;
    }
    if (currentPsshSetPos == PSSHSET_POS_INVALID)
    {
      LOG::LogF(LOGWARNING, "Representation id \"%s\" skipped, invalid PSSHSet.",
                repr->GetId().data());
      return;
    }
    if (isSecureDecoderNeeded)
    {
      LOG::LogF(LOGERROR, "The <ContentProtection><widevine:license> tag must be child of "
                          "the <AdaptationSet> tag.");
    }
  }

  // Parse <AudioChannelConfiguration> tag
  xml_node nodeAudioCh = nodeRepr.child("AudioChannelConfiguration");
  if (nodeAudioCh)
    adpSet->SetAudioChannels(ParseAudioChannelConfig(nodeAudioCh));
  else if (adpSet->GetStreamType() == StreamType::AUDIO && repr->GetAudioChannels() == 0)
    repr->SetAudioChannels(2); // Fallback to 2 channels when no value is set

  // Generate timeline segments
  if (!repr->HasSegmentTimeline() && repr->HasSegmentTemplate())
  {
    auto& segTemplate = repr->GetSegmentTemplate();

    uint64_t reprTotalTimeSecs = m_totalTimeSecs;
    if (period->GetDuration() > 0)
      reprTotalTimeSecs = period->GetDuration() / period->GetTimescale();

    if (!segTemplate->GetMedia().empty() && reprTotalTimeSecs > 0 &&
        segTemplate->GetTimescale() > 0 &&
        (segTemplate->GetDuration() > 0 || adpSet->HasSegmentTimelineDuration()))
    {
      size_t segmentsCount = adpSet->SegmentTimelineDuration().GetSize();
      if (segmentsCount == 0)
      {
        double lengthSecs =
            static_cast<double>(segTemplate->GetDuration()) / segTemplate->GetTimescale();
        segmentsCount =
            static_cast<size_t>(std::ceil(static_cast<double>(reprTotalTimeSecs) / lengthSecs));
      }

      if (segmentsCount < 65536) // SIDX atom is limited to 65535 references (fragments)
      {
        if (!segTemplate->GetInitialization().empty())
        {
          CSegment seg;
          seg.SetIsInitialization(true);
          seg.startPTS_ = 0;
          repr->SetInitSegment(seg);
        }

        CSegment segTl;
        segTl.m_time = adpSet->GetStartPTS();
        segTl.m_number = repr->GetStartNumber();
        segTl.startPTS_ = adpSet->GetStartPTS();

        if (m_isLive && !segTemplate->HasVariableTime() &&
            segTemplate->GetDuration() > 0)
        {
          uint64_t sampleTime = period->GetStart() / 1000;
          segTl.m_number +=
              static_cast<uint64_t>(static_cast<int64_t>(stream_start_ - available_time_ -
                                                         reprTotalTimeSecs - sampleTime) *
                                        segTemplate->GetTimescale() / segTemplate->GetDuration() +
                                    1);
        }
        else if (segTemplate->GetDuration() == 0 && adpSet->HasSegmentTimelineDuration())
        {
          uint32_t duration =
              static_cast<uint32_t>((reprTotalTimeSecs * segTemplate->GetTimescale()) /
                                    adpSet->SegmentTimelineDuration().GetSize());
          segTemplate->SetDuration(duration);
        }

        uint32_t segTplDuration = segTemplate->GetDuration();
        // Reserve memory to speedup
        repr->SegmentTimeline().GetData().reserve(segmentsCount);

        for (size_t pos{0}; pos < segmentsCount; pos++)
        {
          repr->SegmentTimeline().GetData().push_back(segTl);

          uint32_t* tlDuration = adpSet->SegmentTimelineDuration().Get(pos);
          uint32_t duration = tlDuration ? *tlDuration : segTplDuration;
          segTl.m_time += duration;
          segTl.m_number += 1;
          segTl.startPTS_ += duration;
        }

        repr->nextPts_ = segTl.startPTS_;
      }
      else
      {
        LOG::LogF(LOGWARNING,
                  "Cannot generate segments timeline, the segment count exceeds SIDX atom limit.");
      }
    }
    else if (!repr->HasSegmentBase() && !repr->IsSubtitleFileStream())
    {
      //Let us try to extract the fragments out of SIDX atom
      CSegmentBase segBase;
      
      segBase.SetIndexRangeBegin(0);
      //! @todo: Explain the reason of these specific values
      static const uint64_t indexRangeMax = 1024 * 200;
      segBase.SetIndexRangeEnd(indexRangeMax);

      repr->SetSegmentBase(segBase);
    }
  }

  //! @todo: "init prefixed" behaviour is not so clear and may be invalidated by ResolveSegmentBase (?)
  //! we need to investigate if we can move this check where its used by CSession::PrepareStream
  //! and remove HasInitPrefixed statement
  if (repr->HasInitSegment() && !repr->SegmentTimeline().IsEmpty())
  {
    // we assume that we have a MOOV atom included in each segment (max 100k = youtube)
    repr->SetHasInitPrefixed(true);
  }

  // Sanitize period
  if (period->GetTimescale() == 0)
    period->SetTimescale(repr->GetTimescale());
  if (period->GetDuration() == 0)
    period->SetDuration(repr->GetDuration());

  // YouTube fix
  if (repr->GetStartNumber() > m_firstStartNumber)
    m_firstStartNumber = repr->GetStartNumber();

  repr->SetScaling();

  adpSet->AddRepresentation(repr);
}

uint64_t adaptive::CDashTree::ParseTagSegmentTimeline(pugi::xml_node nodeSegTL,
                                                      CSpinCache<uint32_t>& SCTimeline,
                                                      uint32_t timescale /* = 1000 */)
{
  uint64_t startPts{0};
  uint64_t nextPts{0};

  // Parse <S> tags - e.g. <S t="3600" d="900000" r="2398"/>
  for (xml_node node : nodeSegTL.children("S"))
  {
    uint64_t time = XML::GetAttribUint64(node, "t");
    uint32_t duration = XML::GetAttribUint32(node, "d");
    uint32_t repeat = XML::GetAttribUint32(node, "r");
    repeat += 1;

    if (SCTimeline.IsEmpty())
    {
      if (duration > 0)
      {
        // Reserve memory to speedup
        SCTimeline.GetData().reserve(EstimateSegmentsCount(duration, timescale));
      }
      startPts = time;
      nextPts = time;
    }
    else if (time > 0)
    {
      //Go back to the previous timestamp to calculate the real gap.
      nextPts -= SCTimeline.GetData().back();
      SCTimeline.GetData().back() = static_cast<uint32_t>(time - nextPts);
      nextPts = time;
    }
    if (duration > 0)
    {
      for (; repeat > 0; --repeat)
      {
        SCTimeline.GetData().emplace_back(duration);
        nextPts += duration;
      }
    }
  }

  return startPts;
}

uint64_t adaptive::CDashTree::ParseTagSegmentTimeline(xml_node nodeSegTL,
                                                      CSpinCache<CSegment>& SCTimeline,
                                                      uint32_t timescale /* = 1000 */,
                                                      uint64_t totalTimeSecs /* = 0 */,
                                                      CSegmentTemplate* segTemplate /* = nullptr */)
{
  uint64_t startPts{0};
  uint64_t startNumber{0};
  if (segTemplate)
    startNumber = segTemplate->GetStartNumber();

  // Parse <S> tags - e.g. <S t="3600" d="900000" r="2398"/>
  uint64_t nextPts{0};
  for (xml_node node : nodeSegTL.children("S"))
  {
    XML::QueryAttrib(node, "t", nextPts);
    uint32_t duration = XML::GetAttribUint32(node, "d");
    uint32_t repeat = XML::GetAttribUint32(node, "r");
    repeat += 1;

    CSegment seg;

    if (duration > 0 && repeat > 0)
    {
      if (SCTimeline.IsEmpty())
      {
        // Reserve memory to speedup operations
        if (segTemplate && segTemplate->GetDuration() > 0 && segTemplate->GetTimescale() > 0)
        {
          SCTimeline.GetData().reserve(EstimateSegmentsCount(
              segTemplate->GetDuration(), segTemplate->GetTimescale(), totalTimeSecs));
        }
        else
          SCTimeline.GetData().reserve(EstimateSegmentsCount(duration, timescale, totalTimeSecs));

        seg.m_number = startNumber;
      }
      else
        seg.m_number = SCTimeline.GetData().back().m_number + 1;

      seg.m_time = nextPts;
      seg.startPTS_ = nextPts;

      for (; repeat > 0; --repeat)
      {
        SCTimeline.GetData().push_back(seg);
        startPts = seg.startPTS_;

        nextPts += duration;
        seg.m_time = nextPts;
        seg.m_number += 1;
        seg.startPTS_ += duration;
      }
    }
    else
    {
      LOG::LogF(LOGDEBUG, "Missing duration / time on <S> child of <SegmentTimeline> tag.");
    }
  }

  return startPts;
}

void adaptive::CDashTree::ParseSegmentTemplate(pugi::xml_node node, CSegmentTemplate* segTpl)
{
  uint32_t timescale;
  if (XML::QueryAttrib(node, "timescale", timescale))
    segTpl->SetTimescale(timescale);

  if (segTpl->GetTimescale() == 0)
    segTpl->SetTimescale(1); // if not specified defaults to seconds

  uint32_t duration;
  if (XML::QueryAttrib(node, "duration", duration))
    segTpl->SetDuration(duration);

  std::string media;
  if (XML::QueryAttrib(node, "media", media))
    segTpl->SetMedia(media);

  uint32_t startNumber;
  if (XML::QueryAttrib(node, "startNumber", startNumber))
    segTpl->SetStartNumber(startNumber);

  std::string initialization;
  if (XML::QueryAttrib(node, "initialization", initialization))
    segTpl->SetInitialization(initialization);
}

bool adaptive::CDashTree::ParseTagContentProtection(pugi::xml_node nodeParent,
                                                    std::string& currentPssh,
                                                    std::string& currentDefaultKID,
                                                    bool& isSecureDecoderNeeded)
{
  // Parse <ContentProtection> tags to find "default_KID" attribute
  // We try read "default_KID" attribute on every ContentProtection type
  const char* defaultKID{nullptr};

  for (xml_node nodeCP : nodeParent.children("ContentProtection"))
  {
    if (defaultKID)
      break;

    std::string_view schemeIdUri = XML::GetAttrib(nodeCP, "schemeIdUri");

    if (schemeIdUri == "urn:mpeg:dash:mp4protection:2011" ||
        STRING::CompareNoCase(schemeIdUri, m_supportedKeySystem))
    {
      // Parse first attribute that end with "... default_KID"
      // e.g. cenc:default_KID="01004b6f-0835-b807-9098-c070dc30a6c7"
      xml_attribute attrKID = XML::FirstAttributeNoPrefix(nodeCP, "default_KID");
      if (attrKID)
        defaultKID = attrKID.value();

      // get crypto mode if available
      std::string_view protectionValue = XML::GetAttrib(nodeCP, "value");
      if (protectionValue == "cenc")
        m_cryptoMode = CryptoMode::AES_CTR;
      else if (protectionValue == "cbcs")
        m_cryptoMode = CryptoMode::AES_CBC;
    }
  }

  // Parse <ContentProtection> tags
  bool isUrnProtectionFound{false};
  bool isUrnSchemeFound{false};
  std::string commonPssh;
  std::string playReadyPro;

  for (xml_node nodeCP : nodeParent.children("ContentProtection"))
  {
    std::string_view schemeIdUri = XML::GetAttrib(nodeCP, "schemeIdUri");

    if (schemeIdUri == "urn:mpeg:dash:mp4protection:2011")
      isUrnProtectionFound = true;

    // Find Content protection compatible with current systemid
    if (!STRING::CompareNoCase(schemeIdUri, m_supportedKeySystem))
      continue;

    isUrnSchemeFound = true;

    // Parse child tags
    for (xml_node node : nodeCP.children())
    {
      std::string childName = node.name();

      if (StringUtils::EndsWith(childName, "pssh")) // e.g. <cenc:pssh> or <pssh> ...
      {
        commonPssh = node.child_value();
      }
      else if (childName == "mspr:pro")
      {
        playReadyPro = node.child_value();
      }
      else if (childName == "widevine:license")
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
        isSecureDecoderNeeded = robustnessLevel == "HW_SECURE_CODECS_REQUIRED";
      }
    }
  }

  if (commonPssh.empty() && !playReadyPro.empty())
  {
    PRProtectionParser parser;
    if (parser.ParseHeader(playReadyPro))
      currentDefaultKID = parser.GetKID();
  }
  else
  {
    if (!commonPssh.empty())
      currentPssh = commonPssh;

    if ((isUrnSchemeFound || isUrnProtectionFound) && defaultKID && std::strlen(defaultKID) == 36)
    {
      currentDefaultKID.resize(16);
      for (size_t i{0}; i < 16; i++)
      {
        if (i == 4 || i == 6 || i == 8 || i == 10)
          defaultKID++;
        currentDefaultKID[i] = STRING::ToHexNibble(*defaultKID) << 4;
        defaultKID++;
        currentDefaultKID[i] |= STRING::ToHexNibble(*defaultKID);
        defaultKID++;
      }
    }
  }

  return isUrnSchemeFound || isUrnProtectionFound;
}

uint32_t adaptive::CDashTree::ParseAudioChannelConfig(pugi::xml_node node)
{
  std::string_view schemeIdUri = XML::GetAttrib(node, "schemeIdUri");
  std::string_view value = XML::GetAttrib(node, "value");
  uint32_t channels{0};

  if (schemeIdUri == "urn:mpeg:dash:23003:3:audio_channel_configuration:2011" ||
      schemeIdUri == "urn:mpeg:mpegB:cicp:ChannelConfiguration")
  {
    channels = STRING::ToUint32(value);
  }
  else if (schemeIdUri == "urn:dolby:dash:audio_channel_configuration:2011" ||
           schemeIdUri == "tag:dolby.com,2014:dash:audio_channel_configuration:2011")
  {
    if (value == "F801")
      channels = 6;
    else if (value == "FE01")
      channels = 8;
  }
  if (channels == 0)
  {
    LOG::LogF(LOGWARNING, "Cannot parse channel configuration \"%s\", fallback to 2 channels.",
              schemeIdUri.data());
    channels = 2;
  }
  return channels;
}

size_t adaptive::CDashTree::EstimateSegmentsCount(uint64_t duration,
                                                  uint32_t timescale,
                                                  uint64_t totalTimeSecs /* = 0 */)
{
  double lengthSecs{static_cast<double>(duration) / timescale};
  if (lengthSecs < 1)
    lengthSecs = 1;

  if (totalTimeSecs == 0)
    totalTimeSecs = std::max(m_totalTimeSecs, static_cast<uint64_t>(1));

  return static_cast<size_t>(totalTimeSecs / lengthSecs);
}

bool adaptive::CDashTree::DownloadManifestUpd(std::string_view url,
                                              const std::map<std::string, std::string>& reqHeaders,
                                              const std::vector<std::string>& respHeaders,
                                              UTILS::CURL::HTTPResponse& resp)
{
  return CURL::DownloadFile(url, reqHeaders, respHeaders, resp);
}

void adaptive::CDashTree::RefreshSegments(PLAYLIST::CPeriod* period,
                                          PLAYLIST::CAdaptationSet* adp,
                                          PLAYLIST::CRepresentation* rep,
                                          PLAYLIST::StreamType type)
{
  if (type == StreamType::VIDEO || type == StreamType::AUDIO)
  {
    m_updThread.ResetStartTime();
    RefreshLiveSegments();
  }
}

// Can be called form update-thread!
//! @todo: check updated variables that are not thread safe
void adaptive::CDashTree::RefreshLiveSegments()
{
  lastUpdated_ = std::chrono::system_clock::now();

  size_t numReplace = SEGMENT_NO_POS;
  uint64_t nextStartNumber = SEGMENT_NO_NUMBER;

  std::unique_ptr<CDashTree> updateTree{std::move(Clone())};

  // Custom manifest update url parameters
  std::string manifestParams = m_manifestUpdParams;

  std::string manifestUrl;
  if (location_.empty())
  {
    manifestUrl = manifest_url_;
    if (!manifestParams.empty())
      manifestUrl = URL::RemoveParameters(manifestUrl, false);
  }
  else
    manifestUrl = location_;

  // YouTube needs segment start number as parameter
  bool urlHaveStartNumber = manifestParams.find("$START_NUMBER$") != std::string::npos;

  if (urlHaveStartNumber)
  {
    for (auto& period : m_periods)
    {
      for (auto& adpSet : period->GetAdaptationSets())
      {
        for (auto& repr : adpSet->GetRepresentations())
        {
          if (repr->GetStartNumber() + repr->SegmentTimeline().GetSize() < nextStartNumber)
            nextStartNumber = repr->GetStartNumber() + repr->SegmentTimeline().GetSize();

          size_t posReplaceable = repr->getCurrentSegmentPos();
          if (posReplaceable == SEGMENT_NO_POS)
            posReplaceable = repr->SegmentTimeline().GetSize();
          else
            posReplaceable += 1;

          if (posReplaceable < numReplace)
            numReplace = posReplaceable;
        }
      }
    }
    LOG::LogF(LOGDEBUG, "Manifest URL start number param set to: %llu (numReplace: %zu)",
              nextStartNumber, numReplace);

    STRING::ReplaceFirst(manifestParams, "$START_NUMBER$", std::to_string(nextStartNumber));
  }
  else
  {
    // Set header data based from previous manifest request

    if (!m_manifestRespHeaders["etag"].empty())
      m_manifestHeaders["If-None-Match"] = "\"" + m_manifestRespHeaders["etag"] + "\"";

    if (!m_manifestRespHeaders["last-modified"].empty())
      m_manifestHeaders["If-Modified-Since"] = m_manifestRespHeaders["last-modified"];
  }

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

  // Youtube returns last smallest number in case the requested data is not available
  if (urlHaveStartNumber && updateTree->m_firstStartNumber < nextStartNumber)
    return;

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
                        { return item->GetStart() && item->GetStart() == updPeriod->GetStart(); });
    }

    CPeriod* period{nullptr};

    if (itPeriod != m_periods.end())
      period = (*itPeriod).get();

    if (!period && updPeriod->GetId().empty() && updPeriod->GetStart() == 0)
    {
      // not found, fallback match based on position
      if (index < m_periods.size())
        period = m_periods[index].get();
    }

    // new period, insert it
    if (!period)
    {
      LOG::LogF(LOGDEBUG, "Inserting new Period (id=%s, start=%ld)", updPeriod->GetId().data(),
                updPeriod->GetStart());

      updPeriod->SetSequence(m_periodCurrentSeq++);
      m_periods.push_back(std::move(updPeriod));
      continue;
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

          if (!updRepr->SegmentTimeline().Get(0))
          {
            LOG::LogF(LOGERROR,
                      "Segment at position 0 not found from (update) representation id: %s",
                      updRepr->GetId().data());
            return;
          }

          // Found representation
          if (itRepr != adpSet->GetRepresentations().end())
          {
            auto repr = (*itRepr).get();

            if (!repr->SegmentTimeline().IsEmpty())
            {
              if (urlHaveStartNumber) // Partitial update
              {
                auto& updReprSegTL = updRepr->SegmentTimeline();

                // Insert new segments
                uint64_t ptsOffset = repr->nextPts_ - updReprSegTL.Get(0)->startPTS_;
                size_t currentPos = repr->getCurrentSegmentPos();
                size_t repFreeSegments{numReplace};

                auto updSegmentIt(updReprSegTL.GetData().begin());
                for (; updSegmentIt != updReprSegTL.GetData().end() && repFreeSegments != 0;
                      updSegmentIt++)
                {
                  LOG::LogF(LOGDEBUG, "Insert representation (id: %s url: %s)",
                            updRepr->GetId().data(), updSegmentIt->url.c_str());
                  if (repr->HasSegmentsUrl())
                  {
                    CSegment* segment = repr->SegmentTimeline().Get(0);
                    if (!segment)
                    {
                      LOG::LogF(LOGERROR,
                                "Segment at position 0 not found from representation id: %s",
                                repr->GetId().data());
                      return;
                    }
                    segment->url.clear();
                  }
                  updSegmentIt->startPTS_ += ptsOffset;
                  repr->SegmentTimeline().Insert(*updSegmentIt);

                  if (repr->HasSegmentsUrl())
                    updSegmentIt->url.clear();

                  repr->SetStartNumber(repr->GetStartNumber() + 1);
                  repFreeSegments--;
                }

                // We have renewed the current segment
                if (!repFreeSegments && numReplace == currentPos + 1)
                  repr->current_segment_ = nullptr;

                if ((repr->IsWaitForSegment()) && repr->get_next_segment(repr->current_segment_))
                {
                  repr->SetIsWaitForSegment(false);
                  LOG::LogF(LOGDEBUG, "End WaitForSegment stream %s", repr->GetId().data());
                }

                if (updSegmentIt == updReprSegTL.GetData().end())
                  repr->nextPts_ += updRepr->nextPts_;
                else
                  repr->nextPts_ += updSegmentIt->startPTS_;
              }
              else if (updRepr->GetStartNumber() <= 1)
              {
                // Full update, be careful with start numbers!

                //! @todo: check if first element or size differs
                uint64_t segmentId = repr->getCurrentSegmentNumber();

                if (repr->HasSegmentTimeline())
                {
                  uint64_t search_pts = updRepr->SegmentTimeline().Get(0)->m_time;
                  uint64_t misaligned = 0;
                  for (const auto& segment : repr->SegmentTimeline().GetData())
                  {
                    if (misaligned)
                    {
                      uint64_t ptsDiff = segment.m_time - (&segment - 1)->m_time;
                      // our misalignment is small ( < 2%), let's decrement the start number
                      if (misaligned < (ptsDiff * 2 / 100))
                        repr->SetStartNumber(repr->GetStartNumber() - 1);
                      break;
                    }
                    if (segment.m_time == search_pts)
                      break;
                    else if (segment.m_time > search_pts)
                    {
                      if (&repr->SegmentTimeline().GetData().front() == &segment)
                      {
                        repr->SetStartNumber(repr->GetStartNumber() - 1);
                        break;
                      }
                      misaligned = search_pts - (&segment - 1)->m_time;
                    }
                    else
                      repr->SetStartNumber(repr->GetStartNumber() + 1);
                  }
                }
                else if (updRepr->SegmentTimeline().Get(0)->startPTS_ ==
                          repr->SegmentTimeline().Get(0)->startPTS_)
                {
                  uint64_t search_re = updRepr->SegmentTimeline().Get(0)->m_number;
                  for (const auto& segment : repr->SegmentTimeline().GetData())
                  {
                    if (segment.m_number >= search_re)
                      break;
                    repr->SetStartNumber(repr->GetStartNumber() + 1);
                  }
                }
                else
                {
                  uint64_t search_pts = updRepr->SegmentTimeline().Get(0)->startPTS_;
                  for (const auto& segment : repr->SegmentTimeline().GetData())
                  {
                    if (segment.startPTS_ >= search_pts)
                      break;
                    repr->SetStartNumber(repr->GetStartNumber() + 1);
                  }
                }

                updRepr->SegmentTimeline().GetData().swap(repr->SegmentTimeline().GetData());
                if (segmentId == SEGMENT_NO_NUMBER || segmentId < repr->GetStartNumber())
                  repr->current_segment_ = nullptr;
                else
                {
                  if (segmentId >= repr->GetStartNumber() + repr->SegmentTimeline().GetSize())
                    segmentId = repr->GetStartNumber() + repr->SegmentTimeline().GetSize() - 1;
                  repr->current_segment_ =
                      repr->get_segment(static_cast<size_t>(segmentId - repr->GetStartNumber()));
                }

                if (repr->IsWaitForSegment() && repr->get_next_segment(repr->current_segment_))
                  repr->SetIsWaitForSegment(false);

                LOG::LogF(LOGDEBUG,
                          "Full update without start number (repr. id: %s current start: %u)",
                          updRepr->GetId().data(), repr->GetStartNumber());
                m_totalTimeSecs = updateTree->m_totalTimeSecs;
              }
              else if (updRepr->GetStartNumber() > repr->GetStartNumber() ||
                        (updRepr->GetStartNumber() == repr->GetStartNumber() &&
                        updRepr->SegmentTimeline().GetSize() > repr->SegmentTimeline().GetSize()))
              {
                uint64_t segmentId = repr->getCurrentSegmentNumber();

                updRepr->SegmentTimeline().GetData().swap(repr->SegmentTimeline().GetData());
                repr->SetStartNumber(updRepr->GetStartNumber());

                if (segmentId == SEGMENT_NO_NUMBER || segmentId < repr->GetStartNumber())
                  repr->current_segment_ = nullptr;
                else
                {
                  if (segmentId >= repr->GetStartNumber() + repr->SegmentTimeline().GetSize())
                    segmentId = repr->GetStartNumber() + repr->SegmentTimeline().GetSize() - 1;
                  repr->current_segment_ =
                      repr->get_segment(static_cast<size_t>(segmentId - repr->GetStartNumber()));
                }

                if (repr->IsWaitForSegment() && repr->get_next_segment(repr->current_segment_))
                {
                  repr->SetIsWaitForSegment(false);
                }
                LOG::LogF(LOGDEBUG,
                          "Full update with start number (repr. id: %s current start:%u)",
                          updRepr->GetId().data(), repr->GetStartNumber());
                m_totalTimeSecs = updateTree->m_totalTimeSecs;
              }
            }
          }
        }
      }
    }
  }
}

uint64_t adaptive::CDashTree::GetTimestamp()
{
  return UTILS::GetTimestamp();
}
