/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "HLSTree.h"

#include "CompKodiProps.h"
#include "SrvBroker.h"
#include "aes_decrypter.h"
#include "decrypters/HelperPr.h"
#include "decrypters/Helpers.h"
#include "kodi/tools/StringUtils.h"
#include "utils/Base64Utils.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/Utils.h"
#include "utils/XMLUtils.h"
#include "utils/log.h"

#include <algorithm>
#include <optional>
#include <sstream>

using namespace PLAYLIST;
using namespace UTILS;
using namespace kodi::tools;

namespace
{
// Timescale for ms
constexpr uint64_t TIMESCALE = 1000;

// \brief Parse a tag (e.g. #EXT-X-VERSION:1) to extract name and value
void ParseTagNameValue(const std::string& line, std::string& tagName, std::string& tagValue)
{
  tagName.clear();
  tagValue.clear();

  if (line[0] != '#')
    return;

  size_t charPos = line.find(':');
  tagName = line.substr(0, charPos);
  if (charPos != std::string::npos)
    tagValue = line.substr(charPos + 1);
}

// \brief Parse a tag value of attributes, double accent characters will be removed
//        e.g. TYPE=AUDIO,GROUP-ID="audio" the output will be TYPE -> AUDIO and GROUP-ID -> audio
std::map<std::string, std::string> ParseTagAttributes(const std::string& tagValue)
{
  std::map<std::string, std::string> tagAttribs;
  size_t offset{0};
  size_t value;
  size_t end;

  while (offset < tagValue.size() && (value = tagValue.find('=', offset)) != std::string::npos)
  {
    while (offset < tagValue.size() && tagValue[offset] == ' ')
    {
      ++offset;
    }
    end = value;
    uint8_t inValue(0);
    while (++end < tagValue.size() && ((inValue & 1) || tagValue[end] != ','))
    {
      if (tagValue[end] == '\"')
        ++inValue;
    }

    std::string attribName = tagValue.substr(offset, value - offset);
    StringUtils::TrimRight(attribName);

    std::string attribValue =
        tagValue.substr(value + (inValue ? 2 : 1), end - value - (inValue ? 3 : 1));
    StringUtils::Trim(attribValue);

    tagAttribs[attribName] = attribValue;
    offset = end + 1;
  }
  return tagAttribs;
}

void ParseResolution(int& width, int& height, std::string_view val)
{
  size_t pos = val.find('x');
  if (pos != std::string::npos)
  {
    width = STRING::ToInt32(val.substr(0, pos));
    height = STRING::ToInt32(val.substr(pos + 1));
  }
}

// \brief Detect container type from file extension
ContainerType DetectContainerTypeFromExt(std::string_view extension)
{
  if (STRING::CompareNoCase(extension, "ts"))
    return ContainerType::TS;
  else if (STRING::CompareNoCase(extension, "aac"))
    return ContainerType::ADTS;
  else if (STRING::CompareNoCase(extension, "mp4") || STRING::CompareNoCase(extension, "m4s"))
    return ContainerType::MP4;
  else if (STRING::CompareNoCase(extension, "vtt") || STRING::CompareNoCase(extension, "webvtt"))
    return ContainerType::TEXT;
  else
    return ContainerType::INVALID;
}

// \brief Get the first audio codec string from CODECS attribute list
std::string GetAudioCodec(std::string_view codecs)
{
  // Some manifests can provide CODECS attribute with more audio codecs
  // it seems that usually the first in order of apparence is the one referring to the specified
  // AUDIO group attribute, so we return the first one found
  // e.g. CODECS="dvh1.05.01,ec-3,mp4a.40.2", ..., AUDIO="eac-3"
  const std::vector<std::string> list = STRING::SplitToVec(codecs, ',');
  for (const std::string& codecStr : list)
  {
    if (CODEC::IsAudio(codecStr))
      return codecStr;
  }
  return "";
}

// \brief Get the video codec string from CODECS attribute list
std::string GetVideoCodec(std::string_view codecs)
{
  const std::vector<std::string> list = STRING::SplitToVec(codecs, ',');
  for (const std::string& codecStr : list)
  {
    if (CODEC::IsVideo(codecStr))
      return codecStr;
  }
  return "";
}

// \brief Get the first subtitle codec string from CODECS attribute list
std::string GetSubtitleCodec(std::string_view codecs)
{
  const std::vector<std::string> list = STRING::SplitToVec(codecs, ',');
  for (const std::string& codecStr : list)
  {
    if (CODEC::IsSubtitleFourCC(codecStr))
      return codecStr;
  }
  return "";
}
} // unnamed namespace

adaptive::CHLSTree::CHLSTree() : AdaptiveTree()
{
  m_isReqPrepareStream = true;
}

adaptive::CHLSTree::CHLSTree(const CHLSTree& left) : AdaptiveTree(left)
{
  m_decrypter = std::make_unique<AESDecrypter>();
}

void adaptive::CHLSTree::Configure(CHOOSER::IRepresentationChooser* reprChooser,
                                   std::string_view manifestUpdateParam)
{
  AdaptiveTree::Configure(reprChooser, manifestUpdateParam);
  m_decrypter = std::make_unique<AESDecrypter>();
}

bool adaptive::CHLSTree::Open(std::string_view url,
                              const std::map<std::string, std::string>& headers,
                              const std::string& data)
{
  SaveManifest(nullptr, data, url);

  manifest_url_ = url;
  base_url_ = URL::GetUrlPath(url.data());

  if (!ParseManifest(data))
  {
    LOG::LogF(LOGERROR, "Failed to parse the manifest file");
    return false;
  }

  if (m_periods.empty())
  {
    LOG::Log(LOGWARNING, "No periods in the manifest");
    return false;
  }

  m_currentPeriod = m_periods[0].get();

  return true;
}

bool adaptive::CHLSTree::PrepareRepresentation(PLAYLIST::CPeriod* period,
                                               PLAYLIST::CAdaptationSet* adp,
                                               PLAYLIST::CRepresentation* rep)
{
  if (!m_isLive && !rep->Timeline().IsEmpty())
    return true;

  if (!ProcessChildManifest(period, adp, rep, SEGMENT_NO_NUMBER))
    return false;

  StartUpdateThread();

  return true;
}

bool adaptive::CHLSTree::DownloadChildManifest(PLAYLIST::CAdaptationSet* adp,
                                               PLAYLIST::CRepresentation* rep,
                                               UTILS::CURL::HTTPResponse& resp)
{
  if (rep->GetSourceUrl().empty())
  {
    LOG::LogF(LOGERROR, "Cannot download child manifest, no source url on representation id \"%s\"",
              rep->GetId().data());
    return false;
  }

  std::string manifestUrl = rep->GetSourceUrl();
  URL::AppendParameters(manifestUrl, m_manifestParams);

  if (!DownloadManifestChild(manifestUrl, m_manifestHeaders, {}, resp))
    return false;

  SaveManifest(adp, resp.data, manifestUrl);
  return true;
}

void adaptive::CHLSTree::FixMediaSequence(std::stringstream& streamData,
                                          uint64_t& mediaSeqNumber,
                                          size_t adpSetPos,
                                          size_t reprPos)
{
  // Get the last segment PTS and number in the last period
  auto& lastPRep = m_periods.back()->GetAdaptationSets()[adpSetPos]->GetRepresentations()[reprPos];
  if (lastPRep->Timeline().IsEmpty())
    return;
  const CSegment* lastSeg = lastPRep->Timeline().GetBack();
  uint64_t segStartPts = lastSeg->startPTS_; // The start PTS refer to date-time
  uint64_t segNumber = lastSeg->m_number;

  std::streampos streamInitPos = streamData.tellg();
  uint64_t dateTime{0};
  uint64_t totalSegs{0};
  bool isSegFound{false};

  // Inspect all manifest data to try to find the segment
  for (std::string line; STRING::GetLine(streamData, line);)
  {
    std::string tagName;
    std::string tagValue;
    ParseTagNameValue(line, tagName, tagValue);

    if (tagName == "#EXT-X-PROGRAM-DATE-TIME")
    {
      dateTime = static_cast<uint64_t>(XML::ParseDate(tagValue, 0) * 1000);
    }
    else if (tagName == "#EXTINF")
    {
      if (dateTime >= segStartPts)
      {
        isSegFound = true;
        break;
      }

      dateTime += static_cast<uint64_t>(STRING::ToFloat(tagValue) * 1000);
      ++totalSegs;
    }
  }

  // Rollback the stream reader to the initial position
  // to allow the parser to continue from where it stopped
  streamData.clear();
  streamData.seekg(streamInitPos, std::ios::beg);

  if (isSegFound)
  {
    uint64_t mediaSeqNumberFix = segNumber - totalSegs;

    if (mediaSeqNumber != mediaSeqNumberFix)
    {
      LOG::Log(LOGWARNING, "Inconsistent EXT-X-MEDIA-SEQUENCE of %llu, corrected to %llu",
               mediaSeqNumber, mediaSeqNumberFix);
      mediaSeqNumber = mediaSeqNumberFix;
    }
  }
  else
  {
    LOG::Log(LOGERROR, "Inconsistent EXT-X-MEDIA-SEQUENCE of %llu, cannot be corrected");
  }
}

void adaptive::CHLSTree::FixDiscSequence(std::stringstream& streamData, uint32_t& discSeqNumber)
{
  std::streampos streamInitPos = streamData.tellg();
  uint64_t dateTime{0};
  uint32_t discSeqNumberFix = discSeqNumber;
  bool isDiscFound{false};

  // Inspect manifest data to extrapolate the start/end PTS of all periods
  // based on EXT-X-PROGRAM-DATE-TIME
  std::vector<uint64_t> periodsStartTime;
  std::vector<uint64_t> periodsEndTime;

  for (std::string line; STRING::GetLine(streamData, line);)
  {
    std::string tagName;
    std::string tagValue;
    ParseTagNameValue(line, tagName, tagValue);

    if (tagName == "#EXT-X-PROGRAM-DATE-TIME")
    {
      dateTime = static_cast<uint64_t>(XML::ParseDate(tagValue, 0) * 1000);
    }
    else if (tagName == "#EXTINF")
    {
      if (periodsStartTime.empty())
        periodsStartTime.emplace_back(dateTime);

      dateTime += static_cast<uint64_t>(STRING::ToFloat(tagValue) * 1000);
    }
    else if (tagName == "#EXT-X-DISCONTINUITY")
    {
      periodsEndTime.emplace_back(dateTime);
      periodsStartTime.emplace_back(dateTime);
    }
  }
  periodsEndTime.emplace_back(dateTime);

  // Update with single period
  if (periodsStartTime.size() == 1)
  {
    // Continue with the last one
    isDiscFound = true;
    discSeqNumberFix = m_periods.back()->GetSequence();
  }
  else // Update with multiple periods
  {
    // Try to find a match with the second period by using period start time,
    // it works only when the EXT-X-PROGRAM-DATE-TIME value is precise
    for (auto& period : m_periods)
    {
      if (period->GetStart() == periodsStartTime[1])
      {
        discSeqNumberFix = period->GetSequence() - 1;
        isDiscFound = true;
        break;
      }
    }

    if (!isDiscFound)
    {
      // Use cases when the discontinuity has not found:
      // 1) The second period is a new period
      // 2) EXT-X-PROGRAM-DATE-TIME is inconsistent (almost impossible determine which period to update)
      // The following cycle tries to check if the first (updated) period
      // can fit one of the existing ones, otherwise fallback to the last one.
      // With a malformed manifest update this could cause any kind of playback oddities,
      // such as segments played multiple times, or periods switched before the end of their playback.
      for (auto& period : m_periods)
      {
        for (auto itPeriod = m_periods.begin(); itPeriod != m_periods.end(); ++itPeriod)
        {
          if ((*itPeriod)->GetStart() == NO_VALUE)
            continue;

          auto nextPeriod = itPeriod + 1;

          if (nextPeriod != m_periods.end())
          {
            if ((*itPeriod)->GetStart() <= periodsStartTime[0] &&
                (*itPeriod)->GetStart() < periodsEndTime[0] &&
                (*nextPeriod)->GetStart() >= periodsEndTime[0])
            {
              discSeqNumberFix = (*itPeriod)->GetSequence();
              isDiscFound = true;
              break;
            }
          }
          else
          {
            if ((*itPeriod)->GetStart() <= periodsStartTime[0])
            {
              discSeqNumberFix = (*itPeriod)->GetSequence();
              isDiscFound = true;
            }
          }
        }
        if (isDiscFound)
          break;
      }
    }
  }

  if (!isDiscFound)
  {
    LOG::LogF(LOGERROR, "Cannot find appropriate sequence number, try fallback to the last one");
    discSeqNumberFix = m_periods.back()->GetSequence();
  }

  // Rollback the stream reader to the initial position
  // to allow the parser to continue from where it stopped
  streamData.clear();
  streamData.seekg(streamInitPos, std::ios::beg);

  if (discSeqNumber != discSeqNumberFix)
  {
    LOG::Log(LOGWARNING, "Inconsistent EXT-X-DISCONTINUITY-SEQUENCE of %u, corrected to %u",
             discSeqNumber, discSeqNumberFix);
    discSeqNumber = discSeqNumberFix;
  }
}

bool adaptive::CHLSTree::ProcessChildManifest(PLAYLIST::CPeriod* period,
                                              PLAYLIST::CAdaptationSet* adp,
                                              PLAYLIST::CRepresentation* rep,
                                              uint64_t currentSegNumber)
{
  ParseStatus status = ParseStatus::INVALID;
  size_t maxInvalidStatus = 3;

  while (status == ParseStatus::INVALID && maxInvalidStatus > 0)
  {
    UTILS::CURL::HTTPResponse resp;

    if (!DownloadChildManifest(adp, rep, resp))
      return false;

    status = ParseChildManifest(resp.data, URL::GetUrlPath(resp.effectiveUrl), period, adp, rep);

    if (status == ParseStatus::SUCCESS)
    {
      PrepareSegments(period, adp, rep, currentSegNumber);
    }
    else if (status == ParseStatus::INVALID)
    {
      // Give the provider a minimum amount of time before trying to download it again
      std::this_thread::sleep_for(std::chrono::seconds(1));
      maxInvalidStatus--;
    }
  }

  return status == ParseStatus::SUCCESS;
}

 adaptive::CHLSTree::ParseStatus adaptive::CHLSTree::ParseChildManifest(
    const std::string& data,
    std::string_view sourceUrl,
    PLAYLIST::CPeriod* period,
    PLAYLIST::CAdaptationSet* adp,
    PLAYLIST::CRepresentation* rep)
{
  const auto& manifestCfg = CSrvBroker::GetKodiProps().GetManifestConfig();
  size_t adpSetPos = GetPtrPosition(period->GetAdaptationSets(), adp);
  size_t reprPos = GetPtrPosition(adp->GetRepresentations(), rep);

  if (!m_isLive)
  {
    // VOD streaming must be updated always from the first period
    period = m_periods[0].get();
    adp = period->GetAdaptationSets()[adpSetPos].get();
    rep = adp->GetRepresentations()[reprPos].get();
  }

  rep->SetBaseUrl(sourceUrl);

  EncryptionType currentEncryptionType = EncryptionType::NONE;

  // To know in advance if EXT-X-PROGRAM-DATE-TIME is available
  bool hasProgramDateTime = STRING::Contains(data, "#EXT-X-PROGRAM-DATE-TIME:");
  bool hasEndList{false}; // Determine if there is the EXT-X-ENDLIST tag

  uint64_t programDateTime{NO_VALUE}; // EXT-X-PROGRAM-DATE-TIME in ms or NO_VALUE
  uint64_t currentSegNumber{0};

  uint64_t lastSegNumber{SEGMENT_NO_NUMBER}; // The segment number of last segment in the previous manifest
  uint64_t lastSegStartPts{NO_PTS_VALUE}; // The start PTS of last segment in the previous manifest

  uint64_t mediaSequenceNbr{0};

  CSegContainer newSegments;
  std::optional<CSegment> newSegment;

  // Encryptions used between segments
  std::unordered_map<std::string_view, DRM::DRMInfo> drmInfos; // Key System - DRM info
  std::optional<CAesKeyInfo> aesKey;

  uint32_t discontCount{0};

  bool isExtM3Uformat{false};

  // Skip all segments until an EXT-X-DISCONTINUITY is found, probably a malformed manifest update
  // WARNING: when this is true, the "period" variable is nullptr!
  bool isSkipUntilDiscont{false};

  // Parse child playlist
  std::stringstream streamData{data};

  for (std::string line; STRING::GetLine(streamData, line);)
  {
    // Keep track of current line pos, can be used to go back to previous line
    // if we move forward within the loop code
    std::streampos currentStreamPos = streamData.tellg();

    // Find the extended M3U file initialization tag
    if (!isExtM3Uformat)
    {
      if (STRING::StartsWith(line, "#EXTM3U"))
        isExtM3Uformat = true;
      continue;
    }

    std::string tagName;
    std::string tagValue;
    ParseTagNameValue(line, tagName, tagValue);

    if (tagName == "#EXT-X-KEY" && !isSkipUntilDiscont)
    {
      auto attribs = ParseTagAttributes(tagValue);
      // NOTE: Multiple EXT-X-KEYs can be parsed sequentially
      ProcessEncryption(rep->GetBaseUrl(), attribs, aesKey, drmInfos);
    }
    else if (tagName == "#EXT-X-MAP")
    {
      auto attribs = ParseTagAttributes(tagValue);
      CSegment segInit;

      if (STRING::KeyExists(attribs, "BYTERANGE"))
      {
        if (ParseRangeValues(attribs["BYTERANGE"], segInit.range_end_, segInit.range_begin_))
        {
          segInit.range_end_ = segInit.range_begin_ + segInit.range_end_ - 1;
        }
      }

      if (STRING::KeyExists(attribs, "URI"))
      {
        segInit.SetIsInitialization(true);
        segInit.url = attribs["URI"];
        segInit.startPTS_ = NO_PTS_VALUE;
        rep->SetInitSegment(segInit);
        rep->SetContainerType(ContainerType::MP4);
      }
    }
    else if (tagName == "#EXT-X-MEDIA-SEQUENCE")
    {
      mediaSequenceNbr = STRING::ToUint64(tagValue);

      if (manifestCfg.hlsFixMediaSequence && hasProgramDateTime)
        FixMediaSequence(streamData, mediaSequenceNbr, adpSetPos, reprPos);

      currentSegNumber = mediaSequenceNbr;
    }
    else if (tagName == "#EXT-X-PLAYLIST-TYPE")
    {
      if (STRING::CompareNoCase(tagValue, "VOD"))
      {
        m_isLive = false;
        m_updateInterval = NO_VALUE;
      }
    }
    else if (tagName == "#EXT-X-PROGRAM-DATE-TIME" && !isSkipUntilDiscont)
    {
      programDateTime = static_cast<uint64_t>(XML::ParseDate(tagValue, 0) * 1000);
      // Set or update the period start, only from the first program date time value
      if (period->GetStart() == 0 || period->GetStart() == NO_VALUE)
        period->SetStart(programDateTime);
    }
    else if (tagName == "#EXT-X-TARGETDURATION")
    {
      // Use segment max duration as interval time to do a manifest update
      // see: Reloading the Media Playlist file
      // https://datatracker.ietf.org/doc/html/draft-pantos-http-live-streaming-16#section-6.3.4
      uint64_t newInterval = STRING::ToUint64(tagValue) * 1000;
      if (newInterval < m_updateInterval)
        m_updateInterval = newInterval;
    }
    else if (tagName == "#EXTINF" && !isSkipUntilDiscont)
    {
      const uint64_t durMs = static_cast<uint64_t>(STRING::ToFloat(tagValue) * 1000);
      uint64_t startPts{0};
      if (programDateTime != NO_VALUE && period->GetStart() != NO_VALUE)
      {
        startPts = programDateTime;
      }
      else
      {
        const CSegment* lastSeg = newSegments.GetBack();
        if (lastSeg)
          startPts = lastSeg->m_endPts;
      }

      // Make a new segment
      newSegment = CSegment();
      newSegment->startPTS_ = startPts;
      newSegment->m_endPts = startPts + durMs;
      newSegment->m_number = currentSegNumber++;

      // Reset EXT-X-PROGRAM-DATE-TIME, some playlists do not have this tag on each segment
      programDateTime = NO_VALUE;
    }
    else if (tagName == "#EXT-X-BYTERANGE" && newSegment.has_value())
    {
      ParseRangeValues(tagValue, newSegment->range_end_, newSegment->range_begin_);

      if (newSegment->range_begin_ == NO_VALUE)
      {
        if (newSegments.GetSize() > 0)
          newSegment->range_begin_ = newSegments.Get(newSegments.GetSize() - 1)->range_end_ + 1;
        else
          newSegment->range_begin_ = 0;
      }

      newSegment->range_end_ += newSegment->range_begin_ - 1;
    }
    else if (newSegment.has_value() && !line.empty() && line[0] != '#')
    {
      // We fall here after a EXTINF (and possible EXT-X-BYTERANGE in the middle)

      if (rep->GetContainerType() == ContainerType::NOTYPE)
      {
        // Try find the container type on the representation according to the file extension
        std::string url = URL::RemoveParameters(line);
        // Remove domain on absolute url, to not confuse top-level domain as extension
        url = url.substr(URL::GetBaseDomain(url).size());

        std::string extension;
        size_t extPos = url.rfind('.');
        if (extPos != std::string::npos)
          extension = url.substr(extPos + 1);

        ContainerType containerType = ContainerType::INVALID;

        if (!extension.empty())
        {
          containerType = DetectContainerTypeFromExt(extension);

          // Streams that have a media url encoded as a parameter of the url itself
          // e.g. https://cdn-prod.tv/beacon?streamId=1&rp=https%3A%2F%2Ftest.com%2F167037ac3%2Findex_4_0.ts&sessionId=abc&assetId=OD
          // cannot be detected in safe way, so we try fallback to common containers
        }

        if (containerType == ContainerType::INVALID)
        {
          switch (adp->GetStreamType())
          {
            case StreamType::VIDEO:
            case StreamType::AUDIO:
              LOG::LogF(LOGWARNING, "Cannot detect container type from media url, fallback to TS");
              containerType = ContainerType::TS;
              break;
            case StreamType::SUBTITLE:
              LOG::LogF(LOGWARNING,
                        "Cannot detect container type from media url, fallback to TEXT");
              containerType = ContainerType::TEXT;
              break;
            default:
              break;
          }
        }
        rep->SetContainerType(containerType);
      }
      else if (rep->GetContainerType() == ContainerType::INVALID)
      {
        // Skip EXTINF segment
        newSegment.reset();
        continue;
      }

      newSegment->url = line;

      // The EXT-X-KEY tag might appear before or after the EXTINF tag
      // so its needed set the encryption info just before add the segment to timeline
      newSegment->AESKeyInfo() = aesKey;

      newSegments.Add(*newSegment);
      newSegment.reset();
    }
    else if (tagName == "#EXT-X-DISCONTINUITY-SEQUENCE")
    {
      uint32_t discontSeq = STRING::ToUint32(tagValue);

      // Make sure to set the sequence to the initial period
      if (!initial_sequence_.has_value())
        period->SetSequence(discontSeq);

      if (manifestCfg.hlsFixDiscontSequence && hasProgramDateTime)
        FixDiscSequence(streamData, discontSeq);

      if (!initial_sequence_.has_value())
        initial_sequence_ = discontSeq;

      // Delete periods linked to old discontinuities
      const uint32_t currPeriodSeq = m_currentPeriod->GetSequence();

      for (auto itPeriod = m_periods.begin(); itPeriod != m_periods.end();)
      {
        const uint32_t periodSeq = itPeriod->get()->GetSequence();
        if (periodSeq < discontSeq)
        {
          // Period sequence can be equal to current (is use) sequence when:
          // 1) If you pause the video and after some time you want to continue the playback,
          //    but this period become outdated.
          // 2) Malformed manifest update, corrected by FixDiscSequence force this behavior.
          // So in order to force switching to the next period/sequence the segments must be deleted
          if (periodSeq == currPeriodSeq)
          {
            auto& pCurrAdp = m_currentPeriod->GetAdaptationSets()[adpSetPos];
            auto& pCurrRep = pCurrAdp->GetRepresentations()[reprPos];
            pCurrRep->Timeline().Clear();
            pCurrRep->current_segment_ = nullptr;
            LOG::Log(LOGDEBUG, "Clear outdated period of discontinuity %u",
                     itPeriod->get()->GetSequence());
          }
          else
          {
            LOG::Log(LOGDEBUG, "Deleted period of discontinuity %u",
                     itPeriod->get()->GetSequence());
            itPeriod = m_periods.erase(itPeriod);
            continue;
          }
        }

        itPeriod++;
      }

      period = FindDiscontinuityPeriod(discontSeq);
      if (period)
      {
        adp = period->GetAdaptationSets()[adpSetPos].get();
        rep = adp->GetRepresentations()[reprPos].get();
      }
      else
      {
        LOG::LogF(LOGERROR, "Period of discontinuity %u not found, attempt to advance to the next",
                  discontSeq);
        isSkipUntilDiscont = true;
      }

      m_hasDiscontSeq = true;
      m_discontSeq = discontSeq;
    }
    else if (tagName == "#EXT-X-DISCONTINUITY")
    {
      if (!newSegments.IsEmpty() && !isSkipUntilDiscont)
      {
        // Set the DRM info
        for (const auto& info : drmInfos)
        {
          rep->AddDrmInfo(info.second);
        }

        period->SetSequence(m_discontSeq + discontCount);

        rep->SetDuration(newSegments.GetDuration());

        if (adp->GetStreamType() != StreamType::SUBTITLE)
        {
          uint64_t periodDuration =
              (rep->GetDuration() * period->GetTimescale()) / rep->GetTimescale();
          period->SetDuration(periodDuration);
        }

        FreeSegments(rep);

        rep->SetStartNumber(mediaSequenceNbr);

        // Update MEDIA-SEQUENCE for next period
        mediaSequenceNbr += newSegments.GetSize();

        rep->Timeline().Swap(newSegments);
      }

      isSkipUntilDiscont = false;
      ++discontCount;

      currentSegNumber = mediaSequenceNbr;

      CPeriod* newPeriod = FindDiscontinuityPeriod(m_discontSeq + discontCount);

      if (!newPeriod) // Create new period
      {
        auto newPeriodPtr = CPeriod::MakeUniquePtr();

        // Clone same data structure from previous period (no segment will be copied)
        newPeriodPtr->CopyHLSData(period);
        newPeriod = newPeriodPtr.get();
        m_periods.push_back(std::move(newPeriodPtr));
      }

      newPeriod->SetStart(0);

      CAdaptationSet* newAdpSet = newPeriod->GetAdaptationSets()[adpSetPos].get();
      CRepresentation* newRep = newAdpSet->GetRepresentations()[reprPos].get();

      // Copy the base url from previous period/representation
      newRep->SetBaseUrl(rep->GetBaseUrl());

      // Copy init segment from previous period/representation
      // it must persist until overrided by a new EXT-X-MAP tag
      if (rep->GetInitSegment().has_value())
      {
        newRep->SetInitSegment(*rep->GetInitSegment());
        newRep->SetContainerType(rep->GetContainerType());
      }

      // Set the new period as current
      period = newPeriod;
      adp = newAdpSet;
      rep = newRep;
    }
    else if (tagName == "#EXT-X-ENDLIST")
    {
      hasEndList = true;
    }
  }

  if (!isExtM3Uformat)
  {
    LOG::LogF(LOGERROR, "Non-compliant HLS manifest, #EXTM3U tag not found.");
    return ParseStatus::ERROR;
  }

  if (hasEndList)
  {
    if (manifestCfg.hlsIgnoreEndList)
    {
      LOG::Log(LOGWARNING, "Ignored EXT-X-ENDLIST tag");
    }
    else
    {
      m_isLive = false;
      m_updateInterval = NO_VALUE;
    }
  }

  if (m_isLive && m_updateInterval == NO_VALUE)
    m_updateInterval = 0; // Refresh at each segment

  if (newSegments.IsEmpty() || isSkipUntilDiscont)
  {
    LOG::LogF(LOGERROR, "No segments in the manifest.");

    // Faulty live services can send manifest updates with EXT-X-ENDLIST
    // and without segments despite the live stream is not ended
    if (manifestCfg.hlsIgnoreEndList && hasEndList)
      return ParseStatus::INVALID;

    return ParseStatus::ERROR;
  }

  // Set the DRM info
  for (const auto& info : drmInfos)
  {
    rep->AddDrmInfo(info.second);
  }

  FreeSegments(rep);
  rep->Timeline().Swap(newSegments);
  rep->SetStartNumber(mediaSequenceNbr);

  uint64_t reprDur{0};
  if (rep->Timeline().Get(0))
    reprDur = rep->Timeline().GetBack()->m_endPts - rep->Timeline().GetFront()->startPTS_;

  rep->SetDuration(reprDur);
  period->SetSequence(m_discontSeq + discontCount);

  uint64_t totalTimeMs = 0;
  if (discontCount > 0 || m_hasDiscontSeq)
  {
    if (adp->GetStreamType() != StreamType::SUBTITLE)
    {
      uint64_t periodDuration =
          (rep->GetDuration() * m_periods[discontCount]->GetTimescale()) / rep->GetTimescale();
      m_periods[discontCount]->SetDuration(periodDuration);
    }

    for (auto& p : m_periods)
    {
      totalTimeMs += p->GetDuration() * 1000 / p->GetTimescale();
    }
  }
  else
  {
    totalTimeMs = rep->GetDuration() * 1000 / rep->GetTimescale();
  }

  if (adp->GetStreamType() != StreamType::SUBTITLE)
    m_totalTime = totalTimeMs;

  return ParseStatus::SUCCESS;
}

void adaptive::CHLSTree::PrepareSegments(PLAYLIST::CPeriod* period,
                                         PLAYLIST::CAdaptationSet* adp,
                                         PLAYLIST::CRepresentation* rep,
                                         uint64_t segNumber)
{
  if (segNumber == 0 || segNumber < rep->GetStartNumber() ||
      segNumber == SEGMENT_NO_NUMBER)
  {
    rep->current_segment_ = nullptr;
  }
  else
  {
    if (segNumber >= rep->GetStartNumber() + rep->Timeline().GetSize())
    {
      segNumber = rep->GetStartNumber() + rep->Timeline().GetSize() - 1;
    }

    rep->current_segment_ =
        rep->Timeline().Get(static_cast<size_t>(segNumber - rep->GetStartNumber()));
  }

  //! @todo: m_currentPeriod != m_periods.back().get() condition should be removed from here
  //! this is done on AdaptiveStream::ensureSegment on IsLastSegment check
  if (rep->IsWaitForSegment() &&
      (rep->GetNextSegment() || m_currentPeriod != m_periods.back().get()))
  {
    LOG::LogF(LOGDEBUG, "End WaitForSegment stream id \"%s\"", rep->GetId().data());
    rep->SetIsWaitForSegment(false);
  }
}

void adaptive::CHLSTree::OnDataArrived(uint64_t segNum,
                                       std::optional<CAesKeyInfo>& aesKey,
                                       uint8_t iv[16],
                                       const uint8_t* srcData,
                                       size_t srcDataSize,
                                       std::vector<uint8_t>& segBuffer,
                                       size_t segBufferSize,
                                       bool isLastChunk)
{
  if (aesKey.has_value()) // Encrypted media, decrypt it
  {
    std::lock_guard<TreeUpdateThread> lckUpdTree(GetTreeUpdMutex());

    if (aesKey->key.empty())
    {
      if (STRING::KeyExists(m_aesUrlKeyCache, aesKey->keyUrl))
        aesKey->key = m_aesUrlKeyCache[aesKey->keyUrl];

      if (aesKey->key.empty())
      {
        // RETRY:
        auto drmCfgProp = CSrvBroker::GetKodiProps().GetDrmConfig(DRM::KS_NONE);

        CURL::HTTPResponse resp;

        if (DownloadKey(aesKey->keyUrl, drmCfgProp.license.reqHeaders, {}, resp))
        {
          aesKey->key.assign(resp.data.begin(), resp.data.end());
          m_aesUrlKeyCache[aesKey->keyUrl] = aesKey->key;
        }

        /*
         *! @todo: unclear if could be used by some old addon,
         *!        for now all related code has been commented for a future removal
         *
        else if (pssh.defaultKID_ != "0")
        {
          //! @todo: RenewLicense (addon) callback is not wiki documented, there are addons that could use this?
          //!        currently code fall here when the above download fail, there is no a better behaviour to avoid to do a broken download?
          //!        the defaultKID_ is set with a single "0" instead of provide 16 chars, reason?
          pssh.defaultKID_ = "0";
          if (keyParts.size() >= 5 && !keyParts[4].empty() &&
              m_decrypter->RenewLicense(keyParts[4]))
            goto RETRY;
        }
        */
      }
    }

    /*
    if (pssh.defaultKID_ == "0")
    {
      segBuffer.resize(segBufferSize + srcDataSize, 0);
      return;
    }
    else if (!segBufferSize)
    */
    if (!segBufferSize)
    {
      if (aesKey->iv.empty())
        m_decrypter->ivFromSequence(iv, segNum);
      else
      {
        memset(iv, 0, 16);
        memcpy(iv, aesKey->iv.data(), aesKey->iv.size() < 16 ? aesKey->iv.size() : 16);
      }
    }

    // Decrypter needs preallocated data
    segBuffer.resize(segBufferSize + srcDataSize);

    m_decrypter->decrypt(aesKey->key.data(), iv,
                         reinterpret_cast<const AP4_UI08*>(srcData), segBuffer, segBufferSize,
                         srcDataSize, isLastChunk);
    if (srcDataSize >= 16)
      memcpy(iv, srcData + (srcDataSize - 16), 16);
  }
  else
    AdaptiveTree::OnDataArrived(segNum, aesKey, iv, srcData, srcDataSize, segBuffer, segBufferSize,
                                isLastChunk);
}

void adaptive::CHLSTree::OnStreamChange(PLAYLIST::CPeriod* period,
                                        PLAYLIST::CAdaptationSet* adp,
                                        PLAYLIST::CRepresentation* previousRep,
                                        PLAYLIST::CRepresentation* currentRep)
{
  if (!m_isLive && !currentRep->Timeline().IsEmpty())
    return;

  const uint64_t currentSegNumber = previousRep->GetCurrentSegNumber();

  ProcessChildManifest(period, adp, currentRep, currentSegNumber);
}

void adaptive::CHLSTree::OnRequestSegments(PLAYLIST::CPeriod* period,
                                           PLAYLIST::CAdaptationSet* adp,
                                           PLAYLIST::CRepresentation* rep)
{
  if (rep->IsIncludedStream())
    return;

  // Save the current segment position before parsing the manifest
  // to allow find the right segment on updated playlist segments
  const uint64_t segNumber = rep->GetCurrentSegNumber();

  ProcessChildManifest(period, adp, rep, segNumber);
}

void adaptive::CHLSTree::OnPeriodChange()
{
  if (m_isLive)
    m_aesUrlKeyCache.clear();
}

bool adaptive::CHLSTree::DownloadKey(std::string_view url,
                                     const std::map<std::string, std::string>& reqHeaders,
                                     const std::vector<std::string>& respHeaders,
                                     UTILS::CURL::HTTPResponse& resp)
{
  return CURL::DownloadFile(url, reqHeaders, respHeaders, resp);
}

bool adaptive::CHLSTree::DownloadManifestChild(std::string_view url,
                                               const std::map<std::string, std::string>& reqHeaders,
                                               const std::vector<std::string>& respHeaders,
                                               UTILS::CURL::HTTPResponse& resp)
{
  return CURL::DownloadFile(url, reqHeaders, respHeaders, resp);
}

//! @todo: check updated variables that are not thread safe
void adaptive::CHLSTree::OnUpdateSegments()
{
  lastUpdated_ = std::chrono::system_clock::now();

  std::vector<std::tuple<CAdaptationSet*, CRepresentation*>> refreshList;

  for (auto& adpSet : m_currentPeriod->GetAdaptationSets())
  {
    for (auto& repr : adpSet->GetRepresentations())
    {
      if (repr->IsEnabled())
        refreshList.emplace_back(adpSet.get(), repr.get());
    }
  }

  bool isInvalidUpdate = false;

  for (auto& [adpSet, repr] : refreshList)
  {
    // Save the current segment position before parsing the manifest
    // to allow find the right segment on updated playlist segments
    const uint64_t segNumber = repr->GetCurrentSegNumber();

    if (!ProcessChildManifest(m_currentPeriod, adpSet, repr, segNumber))
    {
      isInvalidUpdate = true;
    }
  }

  if (isInvalidUpdate)
  {
    // Faulty live services could send malformed manifest updates
    // so avoid requesting updates too quickly but you also need to make sure
    // that we have segments to mitigate a buffering problem
    // so try halve the interval time in a temporary way
    m_updateInterval = m_updateInterval / 2;
    // Reset the interval on the next update, to restore the original value
    m_updThread.ResetInterval();
  }
}

bool adaptive::CHLSTree::ParseManifest(const std::string& data)
{
  if (data.find("#EXTM3U") == std::string::npos)
  {
    LOG::LogF(LOGERROR, "Non-compliant HLS manifest, #EXTM3U tag not found.");
    return false;
  }

  if (data.find("#EXTINF") == std::string::npos)
  {
    if (!ParseMultivariantPlaylist(data))
      return false;
  }
  else // Media playlist
  {
    //! @todo: we are downloading two times the same manifest
    //! media playlist parsing code could be reused

    std::unique_ptr<CPeriod> period = CPeriod::MakeUniquePtr();
    // In case of missing EXT-X-PROGRAM-DATE-TIME set start period to 0
    period->SetStart(0);
    period->SetTimescale(TIMESCALE);

    auto newAdpSet = CAdaptationSet::MakeUniquePtr(period.get());
    newAdpSet->SetStreamType(StreamType::VIDEO);

    auto repr = CRepresentation::MakeUniquePtr(newAdpSet.get());
    repr->SetTimescale(TIMESCALE);
    repr->SetSourceUrl(manifest_url_);
    repr->AddCodecs(CODEC::FOURCC_H264);

    repr->assured_buffer_duration_ = m_settings.m_bufferAssuredDuration;
    repr->max_buffer_duration_ = m_settings.m_bufferMaxDuration;

    repr->SetScaling();

    newAdpSet->AddRepresentation(repr);
    period->AddAdaptationSet(newAdpSet);

    // We assume audio is included
    period->m_includedStreamType |= 1U << static_cast<int>(StreamType::AUDIO);
    AddIncludedAudioStream(period, CODEC::FOURCC_MP4A);

    m_periods.push_back(std::move(period));
  }

  // Set Live as default
  m_isLive = true;

  return true;
}

void adaptive::CHLSTree::ProcessEncryption(
    std::string baseUrl,
    std::map<std::string, std::string>& attribs,
    std::optional<CAesKeyInfo>& aesKey,
    std::unordered_map<std::string_view, DRM::DRMInfo>& drmInfos)
{
  std::string_view encryptMethod = attribs["METHOD"];

  // NO ENCRYPTION
  if (encryptMethod == "NONE")
  {
    aesKey.reset();
    drmInfos.clear();
    return;
  }

  // According to specs KEYFORMAT is optional and if not specified defaults implicitly to "identity"
  const std::string keyFormat = attribs["KEYFORMAT"].empty() ? "identity" : attribs["KEYFORMAT"];

  std::vector<uint8_t> uriData;
  std::string uriUrl;

  if (STRING::KeyExists(attribs, "URI"))
  {
    if (!URL::GetUriByteData(attribs["URI"], uriData))
    {
      // No URI with data format, but an URL
      uriUrl = attribs["URI"];
    }
  }

  // AES-128
  if (encryptMethod == "AES-128")
  {
    aesKey = CAesKeyInfo();

    if (!uriData.empty())
    {
      aesKey->key = UTILS::ZeroPadding(uriData, 16);
    }

    if (!uriUrl.empty())
    {
      if (URL::IsUrlRelative(uriUrl))
        uriUrl = URL::Join(baseUrl, uriUrl);

      aesKey->keyUrl = uriUrl;
    }

    aesKey->iv = m_decrypter->convertIV(attribs["IV"]);
  }
  // WIDEVINE
  else if (STRING::CompareNoCase(keyFormat, DRM::URN_WIDEVINE))
  {
    
    DRM::DRMInfo& drmInfo = drmInfos[DRM::KS_WIDEVINE]; // Create or update
    drmInfo.keySystem = DRM::KS_WIDEVINE;
    drmInfo.initData = uriData;

    if (STRING::KeyExists(attribs, "KEYID"))
    {
      std::string keyid = attribs["KEYID"];
      STRING::ToLower(keyid);

      if (STRING::StartsWith(keyid, "0x"))
      {
        keyid.erase(0, 2); // To remove "0x"
        STRING::ReplaceAll(keyid, "-", ""); // UUID format should not be allowed
        drmInfo.defaultKid = keyid;
      }
      else
        LOG::LogF(LOGERROR, "Incorrect KEYID tag format");
    }

    if (encryptMethod == "SAMPLE-AES-CTR")
      drmInfo.cryptoMode = CryptoMode::AES_CTR;
    else if (encryptMethod == "SAMPLE-AES")
      drmInfo.cryptoMode = CryptoMode::AES_CBC;
  }
  // PLAYREADY
  else if (STRING::CompareNoCase(keyFormat, DRM::KS_PLAYREADY))
  {
    if (uriData.empty())
    {
      LOG::LogF(LOGERROR, "Incorrect or unsupported URI tag format");
      return;
    }

    DRM::DRMInfo& drmInfo = drmInfos[DRM::KS_PLAYREADY]; // Create or update
    drmInfo.keySystem = DRM::KS_PLAYREADY;
    drmInfo.initData = DRM::PSSH::Make(DRM::ID_PLAYREADY, {}, uriData);

    DRM::PRHeaderParser parser;

    if (parser.Parse(uriData) && !parser.GetKID().empty())
    {
      drmInfo.licenseServerUri = parser.GetLicenseURL();
      drmInfo.defaultKid = STRING::ToLower(STRING::ToHexadecimal(parser.GetKID()));

      auto encryptionType = parser.GetEncryption();
      if (encryptionType == DRM::PRHeaderParser::EncryptionType::AESCTR)
        drmInfo.cryptoMode = CryptoMode::AES_CTR;
      else if (encryptionType == DRM::PRHeaderParser::EncryptionType::AESCBC)
        drmInfo.cryptoMode = CryptoMode::AES_CBC;
    }
    else
      LOG::LogF(LOGERROR, "Cannot parse Playready header");
  }
  // CLEARKEY
  else if (STRING::CompareNoCase(keyFormat, "identity"))
  {
    DRM::DRMInfo& drmInfo = drmInfos[DRM::KS_CLEARKEY]; // Create or update
    drmInfo.keySystem = DRM::KS_CLEARKEY;

    if (uriUrl.empty())
    {
      drmInfo.initData = uriData;
    }
    else
    {
      if (URL::IsUrlRelative(uriUrl))
        uriUrl = URL::Join(baseUrl, uriUrl);

      UTILS::CURL::HTTPResponse resp;
      if (DownloadKey(uriUrl, {}, {}, resp))
        drmInfo.initData = STRING::ToVecUint8(resp.data);
    }

    if (encryptMethod == "SAMPLE-AES-CTR")
      drmInfo.cryptoMode = CryptoMode::AES_CTR;
    else if (encryptMethod == "SAMPLE-AES")
      drmInfo.cryptoMode = CryptoMode::AES_CBC;
  }
  else // Unsupported encryption
  {
    LOG::LogF(LOGDEBUG, "Unsupported EXT-X-KEY keyformat \"%s\"", keyFormat.c_str());
  }
}

bool adaptive::CHLSTree::ParseRenditon(const Rendition& r,
                                       std::unique_ptr<PLAYLIST::CAdaptationSet>& adpSet,
                                       std::unique_ptr<PLAYLIST::CRepresentation>& repr)
{
  StreamType streamType = StreamType::NOTYPE;
  if (r.m_type == "AUDIO")
    streamType = StreamType::AUDIO;
  else if (r.m_type == "SUBTITLES")
    streamType = StreamType::SUBTITLE;
  else // Not supported
    return false;

  adpSet->SetStreamType(streamType);
  adpSet->SetLanguage(r.m_language);
  adpSet->SetName(r.m_name);
  adpSet->SetIsDefault(r.m_isDefault);
  adpSet->SetIsForced(r.m_isForced);

  if (!r.m_characteristics.empty())
  {
    if (STRING::Contains(r.m_characteristics, "public.accessibility.transcribes-spoken-dialog") ||
        STRING::Contains(r.m_characteristics, "public.accessibility.describes-music-and-sound") ||
        STRING::Contains(r.m_characteristics, "public.accessibility.describes-video"))
    {
      adpSet->SetIsImpaired(true);
    }
  }

  repr->SetTimescale(TIMESCALE);

  if (!r.m_uri.empty())
  {
    std::string uri = r.m_uri;
    if (URL::IsUrlRelative(uri))
      uri = URL::Join(base_url_, uri);

    repr->SetSourceUrl(uri);
  }

  if (streamType == StreamType::AUDIO)
  {
    repr->SetAudioChannels(r.m_channels);
    // Set channels in the adptation set to help distinguish it from other similar renditions
    adpSet->SetAudioChannels(r.m_channels);

    if ((r.m_features & REND_FEATURE_EC3_JOC) == REND_FEATURE_EC3_JOC)
      repr->AddCodecs(CODEC::NAME_EAC3_JOC);
  }

  repr->assured_buffer_duration_ = m_settings.m_bufferAssuredDuration;
  repr->max_buffer_duration_ = m_settings.m_bufferMaxDuration;

  repr->SetScaling();

  return true;
}

bool adaptive::CHLSTree::ParseMultivariantPlaylist(const std::string& data)
{
  std::stringstream streamData{data};
  MultivariantPlaylist pl;

  // Parse text data

  for (std::string line; STRING::GetLine(streamData, line);)
  {
    // Keep track of current line pos, can be used to go back to previous line
    // if we move forward within the loop code
    std::streampos currentStreamPos = streamData.tellg();

    std::string tagName;
    std::string tagValue;
    ParseTagNameValue(line, tagName, tagValue);

    if (tagName == "#EXT-X-MEDIA")
    {
      auto attribs = ParseTagAttributes(tagValue);

      StreamType streamType = StreamType::NOTYPE;
      if (attribs["TYPE"] == "AUDIO")
        streamType = StreamType::AUDIO;
      else if (attribs["TYPE"] == "SUBTITLES")
        streamType = StreamType::SUBTITLE;
      else
        continue; // Skip, other types are not supported

      Rendition rend;
      rend.m_type = attribs["TYPE"];
      rend.m_groupId = attribs["GROUP-ID"];
      rend.m_name = attribs["NAME"];
      rend.m_language = attribs["LANGUAGE"]; // Language code format IETF RFC 5646 (BCP-47)
      if (streamType == StreamType::AUDIO)
      {
        rend.m_channels = STRING::ToUint32(attribs["CHANNELS"], 2);
        if (STRING::Contains(attribs["CHANNELS"], "/JOC"))
          rend.m_features |= REND_FEATURE_EC3_JOC;
      }
      rend.m_isDefault = attribs["DEFAULT"] == "YES";
      rend.m_isForced = attribs["FORCED"] == "YES";
      rend.m_characteristics = attribs["CHARACTERISTICS"];
      rend.m_uri = attribs["URI"];
      std::string uri = attribs["URI"];

      if (!uri.empty())
      {
        // Check if this uri has been already added
        if (std::any_of(pl.m_audioRenditions.cbegin(), pl.m_audioRenditions.cend(),
                        [&uri](const Rendition& v) { return v.m_uri == uri; }) ||
            std::any_of(pl.m_subtitleRenditions.cbegin(), pl.m_subtitleRenditions.cend(),
                        [&uri](const Rendition& v) { return v.m_uri == uri; }))
        {
          rend.m_isUriDuplicate = true;
        }
      }

      if (streamType == StreamType::AUDIO)
        pl.m_audioRenditions.emplace_back(rend);
      else if (streamType == StreamType::SUBTITLE)
        pl.m_subtitleRenditions.emplace_back(rend);
    }
    else if (tagName == "#EXT-X-STREAM-INF")
    {
      auto attribs = ParseTagAttributes(tagValue);

      if (!STRING::KeyExists(attribs, "BANDWIDTH"))
      {
        LOG::LogF(LOGERROR, "Skipped EXT-X-STREAM-INF due to to missing bandwidth attribute (%s)",
                  tagValue.c_str());
        continue;
      }

      std::string uri;
      // Try read on the next stream line, to get the playlist URL address
      if (STRING::GetLine(streamData, line) && !line.empty() && line.front() != '#')
        uri = line;
      else
      {
        LOG::Log(LOGDEBUG, "Skipped EXT-X-STREAM-INF tag due to missing uri (%s)",
                 tagValue.c_str());
        streamData.seekg(currentStreamPos); // rollback stream to previous line position
        continue;
      }

      Variant var;
      var.m_bandwidth = STRING::ToUint32(attribs["BANDWIDTH"]);
      var.m_codecs = attribs["CODECS"];
      var.m_resolution = attribs["RESOLUTION"];
      if (STRING::KeyExists(attribs, "FRAME-RATE"))
      {
        var.m_frameRate = STRING::ToFloat(attribs["FRAME-RATE"]);
        if (var.m_frameRate == 0)
          LOG::LogF(LOGWARNING, "Cannot get FRAME-RATE attribute");
      }
      var.m_groupIdAudio = attribs["AUDIO"];
      var.m_groupIdSubtitles = attribs["SUBTITLES"];
      var.m_uri = uri;

      // Check if this uri has been already added
      if (std::any_of(pl.m_variants.cbegin(), pl.m_variants.cend(),
                      [&uri](const Variant& v) { return v.m_uri == uri; }))
      {
        var.m_isUriDuplicate = true;
      }

      pl.m_variants.emplace_back(var);
    }
  }

  // Create Period / Adaptation sets / Representations

  std::unique_ptr<CPeriod> period = CPeriod::MakeUniquePtr();
  // In case of missing EXT-X-PROGRAM-DATE-TIME set start period to 0
  period->SetStart(0);
  period->SetTimescale(TIMESCALE);

  // Add audio renditions (do not take in account variants references)
  for (const Rendition& r : pl.m_audioRenditions)
  {
    // There may be multiple renditions with the same uri but different GROUP-ID
    if (r.m_isUriDuplicate)
      continue;

    auto newAdpSet = CAdaptationSet::MakeUniquePtr(period.get());
    auto newRepr = CRepresentation::MakeUniquePtr(newAdpSet.get());

    if (!ParseRenditon(r, newAdpSet, newRepr))
      continue;

    // Find the codec string from a variant that references it
    const Variant* varFound = FindVariantByAudioGroupId(r.m_groupId, pl.m_variants);
    std::string codecStr;
    if (varFound)
      codecStr = GetAudioCodec(varFound->m_codecs);
    else
      LOG::LogF(LOGERROR, "Cannot find variant for AUDIO GROUP-ID: %s", r.m_groupId.c_str());

    if (codecStr.empty())
      codecStr = CODEC::FOURCC_MP4A; // Fallback

    newRepr->AddCodecs(codecStr);
    newAdpSet->AddCodecs(codecStr);

    if (r.m_uri.empty()) // EXT-X-MEDIA without URI (audio included to video)
    {
      newRepr->SetIsIncludedStream(true);
      period->m_includedStreamType |= 1U << static_cast<int>(StreamType::AUDIO);
    }
    // Ensure that we dont have already an existing adaptation set with same attributes,
    // usually should happens only when we have more EXT-X-MEDIA without URI (audio included to video)
    // and we need to keep just one
    CAdaptationSet* foundAdpSet =
        CAdaptationSet::FindMergeable(period->GetAdaptationSets(), newAdpSet.get());

    if (foundAdpSet)
    {
      if (newRepr->IsIncludedStream())
        continue; // Repr. with included audio already exists

      newRepr->SetParent(foundAdpSet);
      foundAdpSet->AddRepresentation(newRepr);
    }
    else
    {
      newAdpSet->AddRepresentation(newRepr);
      period->AddAdaptationSet(newAdpSet);
    }
  }

  // Add subtitles renditions (do not take in account variants references)
  for (const Rendition& r : pl.m_subtitleRenditions)
  {
    // There may be multiple renditions with the same uri but different GROUP-ID
    if (r.m_isUriDuplicate)
      continue;

    auto newAdpSet = CAdaptationSet::MakeUniquePtr(period.get());
    auto newRepr = CRepresentation::MakeUniquePtr(newAdpSet.get());

    if (!ParseRenditon(r, newAdpSet, newRepr))
      continue;

    // Find the codec string from a variant that references it
    const Variant* varFound = FindVariantBySubtitleGroupId(r.m_groupId, pl.m_variants);
    std::string codecStr;
    if (varFound)
      codecStr = GetSubtitleCodec(varFound->m_codecs);
    if (codecStr.empty())
      codecStr = CODEC::FOURCC_WVTT; // WebVTT as default subtitle codec

    newRepr->AddCodecs(codecStr);
    newAdpSet->AddCodecs(codecStr);

    newAdpSet->AddRepresentation(newRepr);
    period->AddAdaptationSet(newAdpSet);
  }

  // Add variants
  for (const Variant& var : pl.m_variants)
  {
    // There may be multiple variants with the same uri but different AUDIO group
    if (var.m_isUriDuplicate)
      continue;

    if (var.m_bandwidth == 0)
      LOG::LogF(LOGWARNING, "Variant with malformed bandwidth attribute");

    // Try determine the type of stream from codecs
    StreamType streamType = StreamType::NOTYPE;
    std::string codecVideo;
    std::string codecAudio;

    if (!var.m_codecs.empty())
    {
      codecVideo = GetVideoCodec(var.m_codecs);
      codecAudio = GetAudioCodec(var.m_codecs);

      if (!codecVideo.empty()) // Audio/Video stream
        streamType = StreamType::VIDEO;
      else if (!codecAudio.empty()) // Audio only stream
        streamType = StreamType::AUDIO;
    }
    else
    {
      if (var.m_resolution.empty() && var.m_groupIdSubtitles.empty())
      {
        LOG::LogF(LOGDEBUG, "The EXT-X-STREAM-INF variant does not have enough info to "
                            "determine the stream type, will be set as video");
      }
      // We always assume as video type
      streamType = StreamType::VIDEO;
      codecVideo = CODEC::FOURCC_H264;
      if (codecAudio.empty())
        codecAudio = CODEC::FOURCC_MP4A;
    }

    if (streamType == StreamType::AUDIO) // Audio only variant
    {
      auto newAdpSet = CAdaptationSet::MakeUniquePtr(period.get());
      auto newRepr = CRepresentation::MakeUniquePtr(newAdpSet.get());
      // Initialize a rendition with generic info
      Rendition r;
      r.m_channels = 2;
      r.m_language = LANG_CODE::UNDETERMINED;
      r.m_type = "AUDIO";

      if (!var.m_groupIdAudio.empty())
      {
        // Get info from the specified group id rendition
        const Rendition* rFound = FindRenditionByGroupId(var.m_groupIdAudio, pl.m_audioRenditions);
        if (rFound)
          r = *rFound;
        else
          LOG::LogF(LOGWARNING, "Undefined GROUP-ID \"%s\" in EXT-X-STREAM-INF variant",
                    var.m_groupIdAudio.c_str());
      }

      r.m_uri = var.m_uri;

      if (!ParseRenditon(r, newAdpSet, newRepr))
        continue;

      newAdpSet->AddCodecs(codecAudio);
      newRepr->AddCodecs(codecAudio);
      newRepr->SetBandwidth(var.m_bandwidth);

      // Check if it is mergeable with an existing adp set
      CAdaptationSet* foundAdpSet =
          CAdaptationSet::FindMergeable(period->GetAdaptationSets(), newAdpSet.get());
      if (foundAdpSet)
      {
        newRepr->SetParent(foundAdpSet);
        foundAdpSet->AddRepresentation(newRepr);
      }
      else
      {
        newAdpSet->AddRepresentation(newRepr);
        period->AddAdaptationSet(newAdpSet);
      }
    }
    else if (streamType == StreamType::VIDEO) // Video variant
    {
      if (var.m_groupIdAudio.empty())
      {
        // No audio group specified, we assume audio is included to video stream
        period->m_includedStreamType |= 1U << static_cast<int>(StreamType::AUDIO);
        AddIncludedAudioStream(period, codecAudio);
      }

      // We group all video representations by codec fourcc, similar to the DASH specs
      std::string codecFourcc = codecVideo.substr(0, codecVideo.find('.'));
      // Find existing adaptation set with same codec fourcc ...
      CAdaptationSet* adpSet = CAdaptationSet::FindByCodec(period->GetAdaptationSets(), codecFourcc);
      if (!adpSet) // ... or create a new one
      {
        auto newAdpSet = CAdaptationSet::MakeUniquePtr(period.get());
        newAdpSet->SetStreamType(streamType);
        newAdpSet->AddCodecs(codecFourcc);
        adpSet = newAdpSet.get();
        period->AddAdaptationSet(newAdpSet);
      }

      auto repr = CRepresentation::MakeUniquePtr(adpSet);
      repr->SetTimescale(TIMESCALE);
      repr->AddCodecs(codecVideo);
      repr->SetBandwidth(var.m_bandwidth);

      if (!var.m_resolution.empty())
      {
        int resWidth{0};
        int resHeight{0};
        ParseResolution(resWidth, resHeight, var.m_resolution);
        repr->SetResWidth(resWidth);
        repr->SetResHeight(resHeight);
      }
      if (var.m_frameRate != 0)
      {
        repr->SetFrameRate(static_cast<uint32_t>(var.m_frameRate * 1000));
        repr->SetFrameRateScale(1000);
      }

      repr->assured_buffer_duration_ = m_settings.m_bufferAssuredDuration;
      repr->max_buffer_duration_ = m_settings.m_bufferMaxDuration;
      repr->SetScaling();

      std::string uri = var.m_uri;
      if (URL::IsUrlRelative(uri))
        uri = URL::Join(base_url_, uri);

      repr->SetSourceUrl(uri);
      adpSet->AddRepresentation(repr);
    }
    else
    {
      LOG::LogF(LOGWARNING, "Cannot add EXT-X-STREAM-INF variant due to unhandled data");
    }
  }

  m_periods.push_back(std::move(period));
  return true;
}

void adaptive::CHLSTree::SaveManifest(PLAYLIST::CAdaptationSet* adpSet,
                                      const std::string& data,
                                      std::string_view info)
{
  if (m_pathSaveManifest.empty())
    return;

  std::string fileNameSuffix = "master";
  if (adpSet)
  {
    fileNameSuffix = "child-";
    fileNameSuffix += StreamTypeToString(adpSet->GetStreamType());
  }

  AdaptiveTree::SaveManifest(fileNameSuffix, data, info);
}

void adaptive::CHLSTree::AddIncludedAudioStream(std::unique_ptr<PLAYLIST::CPeriod>& period, std::string codec)
{
  auto newAdpSet = CAdaptationSet::MakeUniquePtr(period.get());
  newAdpSet->SetStreamType(StreamType::AUDIO);
  newAdpSet->SetContainerType(ContainerType::MP4);
  newAdpSet->SetLanguage(LANG_CODE::UNDETERMINED);

  auto repr = CRepresentation::MakeUniquePtr(newAdpSet.get());
  repr->SetTimescale(TIMESCALE);

  repr->AddCodecs(codec);
  repr->SetAudioChannels(2);
  repr->SetIsIncludedStream(true);

  repr->assured_buffer_duration_ = m_settings.m_bufferAssuredDuration;
  repr->max_buffer_duration_ = m_settings.m_bufferMaxDuration;

  repr->SetScaling();

  newAdpSet->AddRepresentation(repr);

  // Ensure that we dont have already an existing adaptation set with same attributes,
  // usually should happens when we have more EXT-X-STREAM-INF with audio included to video
  // and we need to keep just one
  CAdaptationSet* foundAdpSet =
      CAdaptationSet::FindMergeable(period->GetAdaptationSets(), newAdpSet.get());

  if (foundAdpSet && foundAdpSet->GetRepresentations().size() == 1)
  {
    if (foundAdpSet->GetRepresentations()[0]->IsIncludedStream())
      return; // Repr. with included audio already exists
  }
  period->AddAdaptationSet(newAdpSet);
}

PLAYLIST::CPeriod* adaptive::CHLSTree::FindDiscontinuityPeriod(const uint32_t seqNumber)
{
  for (auto& period : m_periods)
  {
    if (period->GetSequence() == seqNumber)
      return period.get();
  }
  return nullptr;
}

const adaptive::CHLSTree::Variant* adaptive::CHLSTree::FindVariantByAudioGroupId(
    std::string groupId, std::vector<Variant>& variants) const
{
  auto itVar =
      std::find_if(variants.cbegin(), variants.cend(),
                   [&groupId](const Variant& item) { return item.m_groupIdAudio == groupId; });
  if (itVar != variants.cend())
    return &(*itVar);

  return nullptr;
}

const adaptive::CHLSTree::Variant* adaptive::CHLSTree::FindVariantBySubtitleGroupId(
    std::string groupId, std::vector<Variant>& variants) const
{
  auto itVar =
      std::find_if(variants.cbegin(), variants.cend(),
                   [&groupId](const Variant& item) { return item.m_groupIdSubtitles == groupId; });
  if (itVar != variants.cend())
    return &(*itVar);

  return nullptr;
}

const adaptive::CHLSTree::Rendition* adaptive::CHLSTree::FindRenditionByGroupId(
    std::string groupId, std::vector<adaptive::CHLSTree::Rendition>& renditions) const
{
  auto itRend =
      std::find_if(renditions.cbegin(), renditions.cend(),
                   [&groupId](const Rendition& item) { return item.m_groupId == groupId; });
  if (itRend != renditions.cend())
    return &(*itRend);

  return nullptr;
}
