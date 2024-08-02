/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SmoothTree.h"

#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/Utils.h"
#include "utils/XMLUtils.h"
#include "utils/log.h"
#include "PRProtectionParser.h"
#include "pugixml.hpp"

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

bool adaptive::CSmoothTree::Open(std::string_view url,
                                 const std::map<std::string, std::string>& headers,
                                 const std::string& data)
{
  // We do not add "info" arg to SaveManifest or corrupt possible UTF16 data
  SaveManifest("", data, "");

  manifest_url_ = url;
  base_url_ = URL::GetUrlPath(url.data());

  if (!ParseManifest(data))
    return false;

  if (m_periods.empty())
  {
    LOG::Log(LOGWARNING, "No periods in the manifest");
    return false;
  }

  m_currentPeriod = m_periods[0].get();

  CreateSegmentTimeline();

  return true;
}

bool adaptive::CSmoothTree::ParseManifest(const std::string& data)
{
  std::unique_ptr<CPeriod> period = CPeriod::MakeUniquePtr();

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

  // Default frequency 10000000 (10Khz)
  period->SetTimescale(XML::GetAttribUint32(nodeSSM, "TimeScale", 10000000));

  period->SetDuration(XML::GetAttribUint64(nodeSSM, "Duration"));

  if (STRING::CompareNoCase(XML::GetAttrib(nodeSSM, "IsLive"), "true"))
  {
    m_isLive = true;
    available_time_ = stream_start_;
  }

  m_totalTime = period->GetDuration() * 1000 / period->GetTimescale();

  // Parse <Protection> tag
  PRProtectionParser protParser;
  xml_node nodeProt = nodeSSM.child("Protection");
  if (nodeProt)
  {
    period->SetEncryptionState(EncryptionState::NOT_SUPPORTED);
    period->SetSecureDecodeNeeded(true);

    pugi::xml_node nodeProtHead = nodeProt.child("ProtectionHeader");
    if (nodeProtHead)
    {
      // SystemID can be wrapped by {}
      if (STRING::Contains(XML::GetAttrib(nodeProtHead, "SystemID"),
                           "9A04F079-9840-4286-AB92-E65BE0885F95"))
      {
        if (protParser.ParseHeader(nodeProtHead.child_value()))
        {
          period->SetEncryptionState(EncryptionState::ENCRYPTED_DRM);
          m_licenseUrl = protParser.GetLicenseURL();
        }
      }
      else
        LOG::LogF(LOGERROR, "Protection header with a SystemID not supported or not implemented.");
    }
  }

  // Parse <StreamIndex> tags
  for (xml_node node : nodeSSM.children("StreamIndex"))
  {
    ParseTagStreamIndex(node, period.get(), protParser);
  }

  if (period->GetAdaptationSets().empty())
  {
    LOG::Log(LOGWARNING, "No adaptation sets in the period.");
    return false;
  }

  m_periods.push_back(std::move(period));

  return true;
}

void adaptive::CSmoothTree::ParseTagStreamIndex(pugi::xml_node nodeSI,
                                                PLAYLIST::CPeriod* period,
                                                const PRProtectionParser& protParser)
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

  uint16_t psshSetPos = PSSHSET_POS_DEFAULT;

  if (protParser.HasProtection() && (adpSet->GetStreamType() == StreamType::VIDEO ||
                                     adpSet->GetStreamType() == StreamType::AUDIO))
  {
    psshSetPos = InsertPsshSet(StreamType::VIDEO_AUDIO, period, adpSet.get(), protParser.GetPSSH(),
                               STRING::ToHexadecimal(protParser.GetKID()));
  }

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
    ParseTagQualityLevel(node, adpSet.get(), timescale, psshSetPos);
  }

  if (adpSet->GetRepresentations().empty())
  {
    LOG::LogF(LOGDEBUG, "No generated representations, adaptation set skipped.");
    return;
  }

  if (m_ptsBase == NO_PTS_VALUE || adpSet->GetStartPTS() < m_ptsBase)
    m_ptsBase = adpSet->GetStartPTS();

  period->AddAdaptationSet(adpSet);
}

void adaptive::CSmoothTree::ParseTagQualityLevel(pugi::xml_node nodeQI,
                                                 PLAYLIST::CAdaptationSet* adpSet,
                                                 const uint32_t timescale,
                                                 const uint16_t psshSetPos)
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

  repr->m_psshSetPos = psshSetPos;

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
    const auto& codecs = repr->GetCodecs();
    if (CODEC::Contains(codecs, CODEC::FOURCC_HEVC) ||
        CODEC::Contains(codecs, CODEC::FOURCC_HEV1) || CODEC::Contains(codecs, CODEC::FOURCC_HVC1))
    {
      repr->SetCodecPrivateData(AnnexbToHvcc(codecPrivateData.c_str()));
    }
    else
      repr->SetCodecPrivateData(AnnexbToAvc(codecPrivateData.c_str()));
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
        uint64_t index = 1;

        for (uint32_t segDuration : adpSet->SegmentTimelineDuration())
        {
          CSegment seg;
          seg.startPTS_ = nextStartPts;
          seg.m_endPts = seg.startPTS_ + segDuration;
          seg.m_time = nextStartPts + m_ptsBase;
          seg.m_number = index;

          repr->Timeline().Add(seg);

          nextStartPts += segDuration;
          index++;
        }
      }
    }
  }
}

bool adaptive::CSmoothTree::InsertLiveFragment(PLAYLIST::CAdaptationSet* adpSet,
                                               PLAYLIST::CRepresentation* repr,
                                               uint64_t fTimestamp,
                                               uint64_t fDuration,
                                               uint32_t fTimescale)
{
  if (!m_isLive)
    return false;

  //! @todo: expired_segments_ should be removed and need to be implemented DVRWindowLength
  //! then add a better way to delete old segments from the timeline based on timeshift window
  //! this also requires taking care of the Dash parser

  const CSegment* lastSeg = repr->Timeline().GetBack();
  if (!lastSeg)
    return false;

  LOG::Log(LOGDEBUG,
           "Fragment info - timestamp: %llu, duration: %llu, timescale: %u (PTS base: %llu)",
           fTimestamp, fDuration, fTimescale, m_ptsBase);

  const uint64_t fStartPts =
      static_cast<uint64_t>(static_cast<double>(fTimestamp) / fTimescale * repr->GetTimescale()) -
      m_ptsBase;

  if (fStartPts <= lastSeg->startPTS_)
    return false;

  repr->expired_segments_++;

  CSegment segCopy = *lastSeg;
  const uint64_t duration =
      static_cast<uint64_t>(static_cast<double>(fDuration) / fTimescale * repr->GetTimescale());

  segCopy.startPTS_ = fStartPts;
  segCopy.m_endPts = segCopy.startPTS_ + duration;
  segCopy.m_time = fTimestamp;
  segCopy.m_number++;

  LOG::Log(LOGDEBUG, "Insert fragment to adaptation set \"%s\" (PTS: %llu, number: %llu)",
           adpSet->GetId().data(), segCopy.startPTS_, segCopy.m_number);

  for (auto& repr : adpSet->GetRepresentations())
  {
    repr->Timeline().Append(segCopy);
  }

  return true;
}
