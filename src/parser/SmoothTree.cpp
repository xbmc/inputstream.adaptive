/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SmoothTree.h"

#include "CompKodiProps.h"
#include "SrvBroker.h"
#include "decrypters/Helpers.h"
#include "decrypters/HelperPr.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/Utils.h"
#include "utils/XMLUtils.h"
#include "utils/log.h"
#include "pugixml.hpp"

#include <numeric> // accumulate

using namespace adaptive;
using namespace pugi;
using namespace PLAYLIST;
using namespace UTILS;

adaptive::CSmoothTree::CSmoothTree() : AdaptiveTree()
{
  m_isTTMLTimeRelative = true;
}

adaptive::CSmoothTree::CSmoothTree(const CSmoothTree& left) : AdaptiveTree(left)
{
}

bool adaptive::CSmoothTree::Open(const std::string& url,
                                 const std::map<std::string, std::string>& headers,
                                 const std::string& data)
{
  // We do not add "info" arg to SaveManifest or corrupt possible UTF16 data
  SaveManifest("", data, "");

  manifest_url_ = url;
  base_url_ = URL::GetUrlPath(url);

  if (!ParseManifest(data))
    return false;

  if (m_periods.empty())
  {
    LOG::Log(LOGWARNING, "No periods in the manifest");
    return false;
  }

  m_currentPeriod = m_periods[0].get();

  return true;
}

bool adaptive::CSmoothTree::ParseManifest(const std::string& data)
{
  xml_document doc;
  xml_parse_result parseRes = doc.load_buffer(data.c_str(), data.size());
  if (parseRes.status != status_ok)
  {
    LOG::LogF(LOGERROR, "Failed to parse the manifest file, error code: %i", parseRes.status);
    return false;
  }

  xml_node nodeSSM = doc.child("SmoothStreamingMedia");
  if (!nodeSSM)
  {
    LOG::LogF(LOGERROR, "Failed to get manifest <SmoothStreamingMedia> tag element.");
    return false;
  }

  std::unique_ptr<CPeriod> period = CPeriod::MakeUniquePtr();
  period->SetIndex(1);

  // Default frequency 10000000 (10Khz)
  period->SetTimescale(XML::GetAttribUint32(nodeSSM, "TimeScale", 10000000));

  if (STRING::CompareNoCase(XML::GetAttrib(nodeSSM, "IsLive"), "true"))
  {
    m_isLive = true;
    available_time_ = stream_start_;
    m_updateInterval = 5000;
  }

  m_mediaPresDuration = XML::GetAttribUint64(nodeSSM, "Duration") * 1000 / period->GetTimescale();

  /*! @todo: future rework needed, we currently manage the TSB based on the segments/chunks
   *!  provided by the manifest that should fall within the duration DVRWindowLength,
   *!  but the DVRWindowLength can be also 0 or omitted that means infinite TSB,
   *!  in this case, we should allow for a broader TSB,
   *!  so segments should be collected until the maximum TSB is covered
   *!  Currently each parser has its own segment management, more likely a new common
   *!  interface should be considered to manage segements and TSB
   */
  if (m_isLive)
  {
    m_dvrWindowLength =
        XML::GetAttribUint64(nodeSSM, "DVRWindowLength") * 1000 / period->GetTimescale();

    if (m_dvrWindowLength == 0) // Zero means infinite TSB
    {
      m_dvrWindowLength = 14400000; // Limit to default 4 hours

      auto& manifestCfg = CSrvBroker::GetKodiProps().GetManifestConfig();
      if (manifestCfg.timeShiftBufferLimit.has_value())
        m_dvrWindowLength = *manifestCfg.timeShiftBufferLimit * 1000;
    }
  }
  else
    period->SetDuration(XML::GetAttribUint64(nodeSSM, "Duration"));

  // Parse <Protection> tag
  DRM::PRHeaderParser protParser;
  std::vector<DRM::DRMInfo> drmInfos;
  xml_node nodeProt = nodeSSM.child("Protection");
  if (nodeProt)
  {
    period->SetSecureDecodeNeeded(true);

    for (xml_node nodePH : nodeProt.children("ProtectionHeader"))
    {
      // SystemID can be wrapped by {}
      std::string_view systemId = XML::GetAttrib(nodePH, "SystemID");
      if (STRING::Contains(systemId, "9A04F079-9840-4286-AB92-E65BE0885F95"))
      {
        if (protParser.Parse(nodePH.child_value()))
        {
          DRM::DRMInfo drmInfo;
          drmInfo.keySystem = DRM::KS_PLAYREADY;
          drmInfo.licenseServerUri = protParser.GetLicenseURL();
          drmInfo.initData = DRM::PSSH::Make(DRM::ID_PLAYREADY, {}, protParser.GetInitData());
          drmInfo.defaultKid = STRING::ToLower(STRING::ToHexadecimal(protParser.GetKID()));

          auto encryptionType = protParser.GetEncryption();
          if (encryptionType == DRM::PRHeaderParser::EncryptionType::AESCTR)
            drmInfo.cryptoMode = CryptoMode::AES_CTR;
          else if (encryptionType == DRM::PRHeaderParser::EncryptionType::AESCBC)
            drmInfo.cryptoMode = CryptoMode::AES_CBC;

          drmInfos.emplace_back(drmInfo);
        }
      }
      else // Several SystemID's should be supported, but it is not clear which ones
      {
        LOG::LogF(LOGWARNING,
                  "Not implemented or unsupported \"ProtectionHeader\" with SystemID \"%s\"",
                  systemId.data());
      }
    }
  }

  // Parse <StreamIndex> tags
  std::set<uint64_t> ptsStartList;
  for (xml_node node : nodeSSM.children("StreamIndex"))
  {
    ParseTagStreamIndex(node, period.get(), drmInfos, ptsStartList);
  }

  if (period->GetAdaptationSets().empty())
  {
    LOG::Log(LOGWARNING, "No adaptation sets in the period.");
    return false;
  }

  if (ptsStartList.size() > 1)
  {
    m_ptsBase = *ptsStartList.begin(); // Select the lower PTS
    LOG::Log(
        LOGDEBUG,
        "StreamIndex tags use async PTS for chunk start, segments will be aligned to PTS: %llu",
        m_ptsBase);
  }

  m_periods.push_back(std::move(period));

  CreateSegmentTimeline();

  UpdateTotalTime();

  return true;
}

void adaptive::CSmoothTree::ParseTagStreamIndex(pugi::xml_node nodeSI,
                                                PLAYLIST::CPeriod* period,
                                                const std::vector<DRM::DRMInfo>& drmInfos,
                                                std::set<uint64_t>& ptsStartList)
{
  std::unique_ptr<CAdaptationSet> adpSet = CAdaptationSet::MakeUniquePtr(period);

  if (nodeSI.attribute("ParentStreamIndex"))
  {
    LOG::LogF(LOGDEBUG, "Skipped <StreamIndex> tag, \"ParentStreamIndex\" attribute is not supported.");
    return;
  }

  adpSet->SetName(XML::GetAttrib(nodeSI, "Name"));
  adpSet->SetId("SI:" + adpSet->GetName());

  std::string_view type = XML::GetAttrib(nodeSI, "Type");
  std::string_view subtype = XML::GetAttrib(nodeSI, "Subtype");

  if (type == "video")
  {
    // Skip know unsupported subtypes
    if (subtype == "ZOET" || // Trick mode
        subtype == "CHAP") // Chapter headings
    {
      LOG::LogF(LOGDEBUG, "Skipped <StreamIndex> tag, Subtype \"%s\" not supported.", subtype.data());
      return;
    }
    adpSet->SetStreamType(StreamType::VIDEO);
  }
  else if (type == "audio")
  {
    adpSet->SetStreamType(StreamType::AUDIO);
  }
  else if (type == "text")
  {
    // Skip know unsupported subtypes
    if (subtype == "SCMD" || // Script commands
        subtype == "CHAP" || // Chapter headings
        subtype == "CTRL" || // Control events (ADS)
        subtype == "DATA" || // Application data
        subtype == "ADI3") // ADS sparse tracks
    {
      LOG::LogF(LOGDEBUG, "Skipped <StreamIndex> tag, Subtype \"%s\" not supported.",
                subtype.data());
      return;
    }
    else if (subtype == "CAPT" || subtype == "DESC") // Captions
    {
      adpSet->SetIsImpaired(true);
    }
    adpSet->SetStreamType(StreamType::SUBTITLE);
  }

  // Language code format ISO 639-2
  adpSet->SetLanguage(XML::GetAttrib(nodeSI, "Language"));

  // Default frequency 10000000 (10Khz)
  uint32_t timescale = XML::GetAttribUint32(nodeSI, "TimeScale", 10000000);

  // uint64_t chunks = XML::GetAttribUint32(nodeSI, "Chunks");

  std::string_view url = XML::GetAttrib(nodeSI, "Url");
  if (!url.empty())
  {
    if (!STRING::Contains(url, "{start time}", false))
    {
      LOG::LogF(LOGERROR,
                "Skipped <StreamIndex> tag, {start time} placeholder is missing in the url.");
      return;
    }
    if (!STRING::Contains(url, "{bitrate}", false))
    {
      LOG::LogF(LOGERROR,
                "Skipped <StreamIndex> tag, {bitrate} placeholder is missing in the url.");
      return;
    }
    adpSet->SetBaseUrl(URL::Join(base_url_, url.data()));
  }

  // Parse <c> tags (Chunk identifier for segment of data)
  uint64_t previousPts{0};
  for (xml_node node : nodeSI.children("c"))
  {
    bool hasDuration{false};
    uint32_t duration{0};
    uint32_t repeatCount = 1;

    uint64_t t{0};
    if (XML::QueryAttrib(node, "t", t))
    {
      if (!adpSet->SegmentTimelineDuration().empty())
      {
        //Go back to the previous timestamp to calculate the real gap.
        previousPts -= adpSet->SegmentTimelineDuration().back();
        adpSet->SegmentTimelineDuration().back() = static_cast<uint32_t>(t - previousPts);
      }
      else
      {
        adpSet->SetStartPTS(t);
        ptsStartList.insert(t);
      }
      previousPts = t;
      hasDuration = true;
    }

    if (XML::QueryAttrib(node, "d", duration))
      hasDuration = true;

    XML::QueryAttrib(node, "r", repeatCount);

    if (hasDuration)
    {
      while (repeatCount--)
      {
        adpSet->SegmentTimelineDuration().emplace_back(duration);
        previousPts += duration;
      }
    }
  }

  if (adpSet->SegmentTimelineDuration().empty())
  {
    LOG::LogF(LOGDEBUG, "No generated timeline, adaptation set skipped.");
    return;
  }

  // Parse <QualityLevel> tags
  for (xml_node node : nodeSI.children("QualityLevel"))
  {
    ParseTagQualityLevel(node, adpSet.get(), timescale, drmInfos);
  }

  if (adpSet->GetRepresentations().empty())
  {
    LOG::LogF(LOGDEBUG, "No generated representations, adaptation set skipped.");
    return;
  }

  if (adpSet->GetCodecs().empty())
  {
    adpSet->AddCodecs(adpSet->GetRepresentations().front()->GetCodecs());
  }

  period->AddAdaptationSet(adpSet);
}

void adaptive::CSmoothTree::ParseTagQualityLevel(pugi::xml_node nodeQI,
                                                 PLAYLIST::CAdaptationSet* adpSet,
                                                 const uint32_t timescale,
                                                 const std::vector<DRM::DRMInfo>& drmInfos)
{
  std::unique_ptr<CRepresentation> repr = CRepresentation::MakeUniquePtr(adpSet);

  repr->SetBaseUrl(adpSet->GetBaseUrl());
  repr->SetTimescale(timescale);

  std::string id = "SI:" + adpSet->GetName() + " - QL:";
  id += XML::GetAttrib(nodeQI, "Index");
  repr->SetId(id);

  repr->SetBandwidth(XML::GetAttribUint32(nodeQI, "Bitrate"));

  std::string fourCc;
  if (XML::QueryAttrib(nodeQI, "FourCC", fourCc))
    repr->AddCodecs(fourCc);

  if (!drmInfos.empty())
  {
    for (auto& drmInfo : drmInfos)
    {
      repr->AddDrmInfo(drmInfo);
    }
  }

  repr->SetResWidth(XML::GetAttribInt(nodeQI, "MaxWidth"));
  repr->SetResHeight(XML::GetAttribInt(nodeQI, "MaxHeight"));

  repr->SetSampleRate(XML::GetAttribUint32(nodeQI, "SamplingRate"));

  if (adpSet->GetStreamType() == StreamType::AUDIO)
  {
    // Fallback to 2 channels when no value
    repr->SetAudioChannels(XML::GetAttribUint32(nodeQI, "Channels", 2));
  }

  repr->SetContainerType(ContainerType::MP4);

  std::string codecPrivateData;
  if (XML::QueryAttrib(nodeQI, "CodecPrivateData", codecPrivateData))
  {
    repr->SetCodecPrivateData(STRING::HexToBytes(codecPrivateData));
  }

  if (CODEC::Contains(repr->GetCodecs(), CODEC::FOURCC_AACL) && repr->GetCodecPrivateData().empty())
  {
    uint16_t esds = 0x1010;
    uint16_t sidx = 4;
    switch (repr->GetSampleRate())
    {
      case 96000:
        sidx = 0;
        break;
      case 88200:
        sidx = 1;
        break;
      case 64000:
        sidx = 2;
        break;
      case 48000:
        sidx = 3;
        break;
      case 44100:
        sidx = 4;
        break;
      case 32000:
        sidx = 5;
        break;
    }

    esds |= (sidx << 7);

    std::vector<uint8_t> codecPrivateData;
    codecPrivateData.resize(2);
    codecPrivateData[0] = esds >> 8;
    codecPrivateData[1] = esds & 0xFF;
    repr->SetCodecPrivateData(codecPrivateData);
  }

  CSegmentTemplate segTpl;

  std::string mediaUrl = repr->GetBaseUrl();
  // Convert markers to DASH template identification tag
  STRING::ReplaceFirst(mediaUrl, "{start time}", "$Time$");
  STRING::ReplaceFirst(mediaUrl, "{bitrate}", "$Bandwidth$");

  segTpl.SetMedia(mediaUrl);

  repr->SetSegmentTemplate(segTpl);

  repr->assured_buffer_duration_ = m_settings.m_bufferAssuredDuration;
  repr->max_buffer_duration_ = m_settings.m_bufferMaxDuration;

  repr->SetScaling();

  adpSet->AddRepresentation(repr);
}

void adaptive::CSmoothTree::CreateSegmentTimeline()
{
  for (auto& period : m_periods)
  {
    for (auto& adpSet : period->GetAdaptationSets())
    {
      for (auto& repr : adpSet->GetRepresentations())
      {
        // Adjust PTS with the StreamIndex with lower PTS to sync streams during playback
        uint64_t nextStartPts = adpSet->GetStartPTS() - m_ptsBase;

        for (uint32_t segDuration : adpSet->SegmentTimelineDuration())
        {
          CSegment seg;
          seg.startPTS_ = nextStartPts;
          seg.m_endPts = seg.startPTS_ + segDuration;
          seg.m_time = nextStartPts + m_ptsBase;

          repr->Timeline().Add(seg);

          nextStartPts += segDuration;
        }

        // Update period duration
        if (adpSet->GetStreamType() == StreamType::VIDEO ||
            adpSet->GetStreamType() == StreamType::AUDIO)
        {
          const uint64_t tlDuration =
              repr->Timeline().GetDuration() * period->GetTimescale() / repr->GetTimescale();
          period->SetTlDuration(tlDuration);
        }
      }
    }
  }
}

void adaptive::CSmoothTree::OnUpdateSegments()
{
  lastUpdated_ = std::chrono::system_clock::now();

  std::unique_ptr<CSmoothTree> updateTree{std::move(Clone())};

  // Download and open the manifest update
  CURL::HTTPResponse resp;
  if (!DownloadManifestUpd(manifest_url_, m_manifestHeaders, {}, resp) ||
      !updateTree->Open(resp.effectiveUrl, resp.headers, resp.data))
  {
    return;
  }

  // Update the local variables from manifest update
  auto& period = m_periods[0];
  auto& updPeriod = updateTree->m_periods[0];

  if (updPeriod->GetAdaptationSets().size() != period->GetAdaptationSets().size())
  {
    LOG::LogF(LOGERROR, "Cannot update adaptation sets, the size dont match");
    return;
  }

  // Update by index, a manifest with the same structure is expected
  const auto& updAdpSets = updPeriod->GetAdaptationSets();
  auto& adpSets = period->GetAdaptationSets();

  for (size_t i = 0; i < updAdpSets.size(); ++i)
  {
    auto& updAdpSet = updAdpSets[i];
    auto& adpSet = adpSets[i];

    if (updAdpSet->GetRepresentations().size() != adpSet->GetRepresentations().size())
    {
      LOG::LogF(LOGERROR, "Cannot update representations, the size dont match");
      break;
    }

    // Update by index, a manifest with the same structure is expected
    const auto& updReprs = updAdpSet->GetRepresentations();
    auto& reprs = adpSet->GetRepresentations();

    for (size_t i = 0; i < updReprs.size(); ++i)
    {
      auto& updRepr = updReprs[i];
      auto& repr = reprs[i];

      if (repr->Timeline().IsEmpty())
      {
        LOG::LogF(LOGDEBUG, "SS update - No timeline (repr. id \"%s\")", repr->GetId().c_str());
        continue;
      }

      if (!repr->current_segment_.has_value()) // Representation not used for playback yet
      {
        repr->Timeline().Swap(updRepr->Timeline());

        LOG::LogF(LOGDEBUG, "SS update - Done (repr. id \"%s\")", updRepr->GetId().c_str());
        continue;
      }

      if (repr->Timeline().GetSize() == updRepr->Timeline().GetSize() &&
          repr->Timeline().Get(0)->startPTS_ == updRepr->Timeline().Get(0)->startPTS_)
      {
        LOG::LogF(LOGDEBUG, "SS update - No new segments (repr. id \"%s\")", repr->GetId().c_str());
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
          // or the video provider provide updates that have misaligned PTS on segments,
          // so small PTS gaps that prevent to find the same segment
          foundSeg = &segment;
          LOG::LogF(LOGDEBUG,
                    "SS update - Misaligned: current seg [PTS %llu] found [PTS %llu] "
                    "(repr. id \"%s\")",
                    segStartPTS, segment.startPTS_, repr->GetId().c_str());
          break;
        }
      }

      if (!foundSeg)
      {
        LOG::LogF(LOGDEBUG, "SS update - No segment found (repr. id \"%s\")",
                  repr->GetId().c_str());
      }
      else
      {
        repr->Timeline().Swap(updRepr->Timeline());
        repr->current_segment_ = *foundSeg;

        // Update period duration
        if (adpSet->GetStreamType() == StreamType::VIDEO ||
            adpSet->GetStreamType() == StreamType::AUDIO)
        {
          const uint64_t tlDuration =
              updRepr->Timeline().GetDuration() * period->GetTimescale() / updRepr->GetTimescale();
          period->SetTlDuration(tlDuration);
        }

        LOG::LogF(LOGDEBUG, "SS update - Done (repr. id \"%s\")", updRepr->GetId().c_str());
      }

      if (repr->IsWaitForSegment() && repr->GetNextSegment())
      {
        repr->SetIsWaitForSegment(false);
        LOG::LogF(LOGDEBUG, "End WaitForSegment repr. id %s", repr->GetId().c_str());
      }
    }
  }

  UpdateTotalTime();
}

bool adaptive::CSmoothTree::DownloadManifestUpd(
    const std::string& url,
    const std::map<std::string, std::string>& reqHeaders,
    const std::vector<std::string>& respHeaders,
    UTILS::CURL::HTTPResponse& resp)
{
  return CURL::DownloadFile(url, reqHeaders, respHeaders, resp);
}

void adaptive::CSmoothTree::UpdateTotalTime()
{
  uint64_t totalDurMs = m_mediaPresDuration;
  if (totalDurMs == 0)
  {
    totalDurMs =
        std::accumulate(m_periods.begin(), m_periods.end(), uint64_t{0},
                        [](uint64_t sum, const std::unique_ptr<CPeriod>& period)
                        { return sum + period->GetTlDuration() * 1000 / period->GetTimescale(); });

    if (m_dvrWindowLength != 0 && totalDurMs > m_dvrWindowLength)
      totalDurMs = m_dvrWindowLength;
  }

  m_totalTime = totalDurMs;
}
