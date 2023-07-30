/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "HLSTree.h"

#include "../aes_decrypter.h"
#include "../utils/Base64Utils.h"
#include "../utils/StringUtils.h"
#include "../utils/UrlUtils.h"
#include "../utils/Utils.h"
#include "../utils/log.h"
#include "kodi/tools/StringUtils.h"

#include <optional>
#include <sstream>

using namespace PLAYLIST;
using namespace UTILS;
using namespace kodi::tools;

namespace
{
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
} // unnamed namespace

adaptive::CHLSTree::CHLSTree(const CHLSTree& left) : AdaptiveTree(left)
{
  m_decrypter = std::make_unique<AESDecrypter>(left.m_decrypter->getLicenseKey());
}

void adaptive::CHLSTree::Configure(const UTILS::PROPERTIES::KodiProperties& kodiProps,
                                   CHOOSER::IRepresentationChooser* reprChooser,
                                   std::string_view supportedKeySystem,
                                   std::string_view manifestUpdateParam)
{
  AdaptiveTree::Configure(kodiProps, reprChooser, supportedKeySystem, manifestUpdateParam);
  m_decrypter = std::make_unique<AESDecrypter>(kodiProps.m_licenseKey);
}

bool adaptive::CHLSTree::Open(std::string_view url,
                              const std::map<std::string, std::string>& headers,
                              const std::string& data)
{
  SaveManifest(nullptr, data, url);

  manifest_url_ = url;
  base_url_ = URL::RemoveParameters(url.data());

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

  // SortTree();

  return true;
}

PLAYLIST::PrepareRepStatus adaptive::CHLSTree::prepareRepresentation(PLAYLIST::CPeriod* period,
                                                                     PLAYLIST::CAdaptationSet* adp,
                                                                     PLAYLIST::CRepresentation* rep,
                                                                     bool update)
{
  if (rep->GetSourceUrl().empty())
    return PrepareRepStatus::FAILURE;

  CRepresentation* entryRep = rep;
  uint64_t currentRepSegNumber = rep->getCurrentSegmentNumber();

  size_t adpSetPos = GetPtrPosition(period->GetAdaptationSets(), adp);
  size_t reprPos = GetPtrPosition(adp->GetRepresentations(), rep);

  std::unique_ptr<CPeriod> periodLost;

  PrepareRepStatus prepareStatus = PrepareRepStatus::OK;
  UTILS::CURL::HTTPResponse resp;

  if (!rep->m_isDownloaded)
  {
    // Download child manifest playlist

    // Since we assume Live content by default we enable manifest update by segment,
    // this setting can be changed by parsing manifest below
    m_updateInterval = 0;

    std::string manifestUrl = rep->GetSourceUrl();
    URL::AppendParameters(manifestUrl, m_manifestParams);

    if (!DownloadManifestChild(manifestUrl, m_manifestHeaders, {}, resp))
      return PrepareRepStatus::FAILURE;

    SaveManifest(adp, resp.data, manifestUrl);

    rep->SetBaseUrl(URL::RemoveParameters(resp.effectiveUrl));

    EncryptionType currentEncryptionType = EncryptionType::CLEAR;

    uint64_t currentSegStartPts{0};
    uint64_t newStartNumber{0};

    CSpinCache<CSegment> newSegments;
    std::optional<CSegment> newSegment;

    // Pssh set used between segments
    uint16_t psshSetPos = PSSHSET_POS_DEFAULT;

    uint32_t discontCount{0};

    bool isExtM3Uformat{false};

    // Parse child playlist
    std::stringstream streamData{resp.data};

    for (std::string line; STRING::GetLine(streamData, line);)
    {
      // Keep track of current line pos, can be used to go back to previous line
      // if we move forward within the loop code
      std::streampos currentStreamPos = streamData.tellg();

      std::string tagName;
      std::string tagValue;
      ParseTagNameValue(line, tagName, tagValue);

      // Find the extended M3U file initialization tag
      if (!isExtM3Uformat)
      {
        if (tagName == "#EXTM3U")
          isExtM3Uformat = true;
        continue;
      }

      if (tagName == "#EXT-X-KEY")
      {
        auto attribs = ParseTagAttributes(tagValue);

        switch (ProcessEncryption(rep->GetBaseUrl(), attribs))
        {
          case EncryptionType::NOT_SUPPORTED:
            period->SetEncryptionState(EncryptionState::ENCRYPTED);
            return PrepareRepStatus::FAILURE;
          case EncryptionType::AES128:
            currentEncryptionType = EncryptionType::AES128;
            psshSetPos = PSSHSET_POS_DEFAULT;
            break;
          case EncryptionType::WIDEVINE:
            currentEncryptionType = EncryptionType::WIDEVINE;
            period->SetEncryptionState(EncryptionState::ENCRYPTED_SUPPORTED);

            rep->m_psshSetPos = InsertPsshSet(adp->GetStreamType(), period, adp, m_currentPssh,
                                              m_currentDefaultKID, m_currentIV);
            if (period->GetPSSHSets()[rep->GetPsshSetPos()].m_usageCount == 1 ||
                prepareStatus == PrepareRepStatus::DRMCHANGED)
            {
              prepareStatus = PrepareRepStatus::DRMCHANGED;
            }
            else
              prepareStatus = PrepareRepStatus::DRMUNCHANGED;
            break;
          case EncryptionType::UNKNOWN:
            LOG::LogF(LOGWARNING, "Unknown encryption type");
            break;
          default:
            break;
        }
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
          segInit.pssh_set_ = PSSHSET_POS_DEFAULT;
          rep->SetInitSegment(segInit);
          rep->SetContainerType(ContainerType::MP4);
        }
      }
      else if (tagName == "#EXT-X-MEDIA-SEQUENCE")
      {
        newStartNumber = STRING::ToUint64(tagValue);
      }
      else if (tagName == "#EXT-X-PLAYLIST-TYPE")
      {
        if (STRING::CompareNoCase(tagValue, "VOD"))
        {
          m_isLive = false;
          m_updateInterval = NO_VALUE;
        }
      }
      else if (tagName == "#EXT-X-TARGETDURATION")
      {
        // Use segment max duration as interval time to do a manifest update
        // see: Reloading the Media Playlist file
        // https://datatracker.ietf.org/doc/html/draft-pantos-http-live-streaming-16#section-6.3.4
        uint64_t newIntervalSecs = STRING::ToUint64(tagValue) * 1000;
        if (newIntervalSecs < m_updateInterval)
          m_updateInterval = newIntervalSecs;
      }
      else if (tagName == "#EXTINF")
      {
        // Make a new segment
        newSegment = CSegment();
        newSegment->startPTS_ = currentSegStartPts;

        uint64_t duration = static_cast<uint64_t>(STRING::ToFloat(tagValue) * rep->GetTimescale());
        newSegment->m_duration = duration;
        newSegment->pssh_set_ = psshSetPos;

        currentSegStartPts += duration;
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
          std::string url = URL::RemoveParameters(line, false);
          // Remove domain on absolute url, to not confuse top-level domain as extension
          url = url.substr(URL::GetDomainUrl(url).size());

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
                LOG::LogF(LOGWARNING,
                          "Cannot detect container type from media url, fallback to TS");
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

        if (currentEncryptionType == EncryptionType::AES128)
        {
          if (psshSetPos == PSSHSET_POS_DEFAULT)
          {
            psshSetPos = InsertPsshSet(StreamType::NOTYPE, period, adp, m_currentPssh,
                                       m_currentDefaultKID, m_currentIV);
            newSegment->pssh_set_ = psshSetPos;
          }
          else
            period->InsertPSSHSet(newSegment->pssh_set_);
        }

        newSegments.GetData().emplace_back(*newSegment);
        newSegment.reset();
      }
      else if (tagName == "#EXT-X-DISCONTINUITY-SEQUENCE")
      {
        m_discontSeq = STRING::ToUint32(tagValue);
        if (!initial_sequence_.has_value())
          initial_sequence_ = m_discontSeq;

        m_hasDiscontSeq = true;
        // make sure first period has a sequence on initial prepare
        if (!update && m_discontSeq > 0 && m_periods.back()->GetSequence() == 0)
          m_periods[0]->SetSequence(m_discontSeq);

        for (auto itPeriod = m_periods.begin(); itPeriod != m_periods.end();)
        {
          if (itPeriod->get()->GetSequence() < m_discontSeq)
          {
            if ((*itPeriod).get() != m_currentPeriod)
            {
              itPeriod = m_periods.erase(itPeriod);
            }
            else
            {
              // we end up here after pausing for some time
              // remove from m_periods for now and reattach later
              periodLost = std::move(*itPeriod); // Move out the period unique ptr
              itPeriod = m_periods.erase(itPeriod); // Remove empty unique ptr state
            }
          }
          else
            itPeriod++;
        }

        period = m_periods[0].get();
        adp = period->GetAdaptationSets()[adpSetPos].get();
        rep = adp->GetRepresentations()[reprPos].get();
      }
      else if (tagName == "#EXT-X-DISCONTINUITY")
      {
        if (!newSegments.Get(0))
        {
          LOG::LogF(LOGERROR, "Segment at position 0 not found");
          continue;
        }

        period->SetSequence(m_discontSeq + discontCount);

        uint64_t duration{0};
        if (!newSegments.IsEmpty())
          duration = currentSegStartPts - newSegments.Get(0)->startPTS_;
        rep->SetDuration(duration);

        if (adp->GetStreamType() != StreamType::SUBTITLE)
        {
          uint64_t periodDuration =
              (rep->GetDuration() * m_periods[discontCount]->GetTimescale()) / rep->GetTimescale();
          period->SetDuration(periodDuration);
        }

        FreeSegments(period, rep);
        rep->SegmentTimeline().Swap(newSegments);
        rep->SetStartNumber(newStartNumber);

        if (m_periods.size() == ++discontCount)
        {
          auto newPeriod = CPeriod::MakeUniquePtr();
          // CopyHLSData will copy also the init segment in the representations
          // that must persist to next period until overrided by new EXT-X-MAP tag
          newPeriod->CopyHLSData(m_currentPeriod);
          period = newPeriod.get();
          m_periods.push_back(std::move(newPeriod));
        }
        else
          period = m_periods[discontCount].get();

        newStartNumber += rep->SegmentTimeline().GetSize();
        adp = period->GetAdaptationSets()[adpSetPos].get();
        rep = adp->GetRepresentations()[reprPos].get();

        currentSegStartPts = 0;

        if (currentEncryptionType == EncryptionType::WIDEVINE)
        {
          rep->m_psshSetPos = InsertPsshSet(adp->GetStreamType(), period, adp, m_currentPssh,
                                            m_currentDefaultKID, m_currentIV);
          period->SetEncryptionState(EncryptionState::ENCRYPTED_SUPPORTED);
        }

        if (rep->HasInitSegment())
          rep->SetContainerType(ContainerType::MP4);
      }
      else if (tagName == "#EXT-X-ENDLIST")
      {
        m_isLive = false;
        m_updateInterval = NO_VALUE;
      }
    }

    if (!isExtM3Uformat)
    {
      LOG::LogF(LOGERROR, "Non-compliant HLS manifest, #EXTM3U tag not found.");
      return PrepareRepStatus::FAILURE;
    }

    FreeSegments(period, rep);

    if (newSegments.IsEmpty())
    {
      LOG::LogF(LOGERROR, "No segments parsed.");
      return PrepareRepStatus::FAILURE;
    }

    rep->SegmentTimeline().Swap(newSegments);
    rep->SetStartNumber(newStartNumber);

    uint64_t reprDuration{0};
    if (rep->SegmentTimeline().Get(0))
      reprDuration = currentSegStartPts - rep->SegmentTimeline().Get(0)->startPTS_;

    rep->SetDuration(reprDuration);
    period->SetSequence(m_discontSeq + discontCount);

    uint64_t totalTimeSecs = 0;
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
        totalTimeSecs += p->GetDuration() / p->GetTimescale();
        if (!m_isLive)
        {
          auto& adpSet = p->GetAdaptationSets()[adpSetPos];
          adpSet->GetRepresentations()[reprPos]->m_isDownloaded = true;
        }
      }
    }
    else
    {
      totalTimeSecs = rep->GetDuration() / rep->GetTimescale();
      if (!m_isLive)
      {
        rep->m_isDownloaded = true;
      }
    }

    if (adp->GetStreamType() != StreamType::SUBTITLE)
      m_totalTimeSecs = totalTimeSecs;
  }

  if (update)
  {
    if (currentRepSegNumber == 0 || currentRepSegNumber < entryRep->GetStartNumber() ||
        currentRepSegNumber == SEGMENT_NO_NUMBER)
    {
      entryRep->current_segment_ = nullptr;
    }
    else
    {
      if (currentRepSegNumber >= entryRep->GetStartNumber() + entryRep->SegmentTimeline().GetSize())
      {
        currentRepSegNumber =
            entryRep->GetStartNumber() + entryRep->SegmentTimeline().GetSize() - 1;
      }

      entryRep->current_segment_ = entryRep->get_segment(
          static_cast<size_t>(currentRepSegNumber - entryRep->GetStartNumber()));
    }
    if (entryRep->IsWaitForSegment() && (entryRep->get_next_segment(entryRep->current_segment_) ||
                                         m_currentPeriod != m_periods.back().get()))
    {
      entryRep->SetIsWaitForSegment(false);
    }
  }
  else
  {
    entryRep->SetIsPrepared(true);
    StartUpdateThread();
  }

  if (periodLost)
    m_periods.insert(m_periods.begin(), std::move(periodLost));

  return prepareStatus;
}

void adaptive::CHLSTree::OnDataArrived(uint64_t segNum,
                                       uint16_t psshSet,
                                       uint8_t iv[16],
                                       const uint8_t* srcData,
                                       size_t srcDataSize,
                                       std::vector<uint8_t>& segBuffer,
                                       size_t segBufferSize,
                                       bool isLastChunk)
{
  if (psshSet && m_currentPeriod->GetEncryptionState() != EncryptionState::ENCRYPTED_SUPPORTED)
  {
    std::lock_guard<TreeUpdateThread> lckUpdTree(GetTreeUpdMutex());

    std::vector<CPeriod::PSSHSet>& psshSets = m_currentPeriod->GetPSSHSets();

    if (psshSet >= psshSets.size())
    {
      LOG::LogF(LOGERROR, "Cannot get PSSHSet at position %u", psshSet);
      return;
    }

    CPeriod::PSSHSet& pssh = psshSets[psshSet];

    //Encrypted media, decrypt it
    if (pssh.defaultKID_.empty())
    {
      //First look if we already have this URL resolved
      for (auto itPsshSet = m_currentPeriod->GetPSSHSets().begin();
           itPsshSet != m_currentPeriod->GetPSSHSets().end(); itPsshSet++)
      {
        if (itPsshSet->pssh_ == pssh.pssh_ && !itPsshSet->defaultKID_.empty())
        {
          pssh.defaultKID_ = itPsshSet->defaultKID_;
          break;
        }
      }

      if (pssh.defaultKID_.empty())
      {
      RETRY:
        std::map<std::string, std::string> headers;
        std::vector<std::string> keyParts = STRING::SplitToVec(m_decrypter->getLicenseKey(), '|');
        std::string url = pssh.pssh_.c_str();

        if (keyParts.size() > 0)
        {
          URL::AppendParameters(url, keyParts[0]);
        }
        if (keyParts.size() > 1)
          ParseHeaderString(headers, keyParts[1]);

        CURL::HTTPResponse resp;

        if (DownloadKey(url, headers, {}, resp))
        {
          pssh.defaultKID_ = resp.data;
        }
        else if (pssh.defaultKID_ != "0")
        {
          pssh.defaultKID_ = "0";
          if (keyParts.size() >= 5 && !keyParts[4].empty() &&
              m_decrypter->RenewLicense(keyParts[4]))
            goto RETRY;
        }
      }
    }
    if (pssh.defaultKID_ == "0")
    {
      segBuffer.resize(segBufferSize + srcDataSize, 0);
      return;
    }
    else if (!segBufferSize)
    {
      if (pssh.iv.empty())
        m_decrypter->ivFromSequence(iv, segNum);
      else
      {
        memset(iv, 0, 16);
        memcpy(iv, pssh.iv.data(), pssh.iv.size() < 16 ? pssh.iv.size() : 16);
      }
    }

    // Decrypter needs preallocated data
    segBuffer.resize(segBufferSize + srcDataSize);

    m_decrypter->decrypt(reinterpret_cast<const uint8_t*>(pssh.defaultKID_.data()), iv,
                         reinterpret_cast<const AP4_UI08*>(srcData), segBuffer, segBufferSize,
                         srcDataSize, isLastChunk);
    if (srcDataSize >= 16)
      memcpy(iv, srcData + (srcDataSize - 16), 16);
  }
  else
    AdaptiveTree::OnDataArrived(segNum, psshSet, iv, srcData, srcDataSize, segBuffer, segBufferSize,
                                isLastChunk);
}

//Called each time before we switch to a new segment
void adaptive::CHLSTree::RefreshSegments(PLAYLIST::CPeriod* period,
                                         PLAYLIST::CAdaptationSet* adp,
                                         PLAYLIST::CRepresentation* rep,
                                         PLAYLIST::StreamType type)
{
  if (rep->IsIncludedStream())
    return;

  m_updThread.ResetStartTime();
  prepareRepresentation(period, adp, rep, true);
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

// Can be called form update-thread!
//! @todo: check updated variables that are not thread safe
void adaptive::CHLSTree::RefreshLiveSegments()
{
  lastUpdated_ = std::chrono::system_clock::now();

  std::vector<std::tuple<CAdaptationSet*, CRepresentation*>> refreshList;

  for (auto& adpSet : m_currentPeriod->GetAdaptationSets())
  {
    for (auto& repr : adpSet->GetRepresentations())
    {
      if (repr->IsEnabled())
        refreshList.emplace_back(std::make_tuple(adpSet.get(), repr.get()));
    }
  }
  for (auto& itemList : refreshList)
  {
    prepareRepresentation(m_currentPeriod, std::get<0>(itemList), std::get<1>(itemList), true);
  }
}

bool adaptive::CHLSTree::ParseManifest(const std::string& data)
{
  // Parse master playlist

  bool isExtM3Uformat{false};

  // Determine if is needed create a dummy audio representation for audio stream embedded on video stream
  bool createDummyAudioRepr{false};

  std::unique_ptr<CPeriod> period = CPeriod::MakeUniquePtr();
  period->SetTimescale(1000000);

  std::stringstream streamData{data};
  // Mapped attributes of each EXT-X-MEDIA tag and grouped by GROUP-ID
  std::map<std::string, std::vector<std::map<std::string, std::string>>> mediaGroups;
  // EXT-X-MEDIA GROUP-ID parsed from EXT-X-STREAM-INF attributes (e.g. AUDIO)
  std::set<std::string> mediaGroupParsed;

  for (std::string line; STRING::GetLine(streamData, line);)
  {
    // Keep track of current line pos, can be used to go back to previous line
    // if we move forward within the loop code
    std::streampos currentStreamPos = streamData.tellg();

    std::string tagName;
    std::string tagValue;
    ParseTagNameValue(line, tagName, tagValue);

    // Find the extended M3U file initialization tag
    if (!isExtM3Uformat)
    {
      if (tagName == "#EXTM3U")
        isExtM3Uformat = true;
      continue;
    }

    if (tagName == "#EXT-X-MEDIA")
    {
      auto attribs = ParseTagAttributes(tagValue);
      if (!STRING::KeyExists(attribs, "GROUP-ID"))
      {
        LOG::LogF(LOGDEBUG, "Skipped EXT-X-MEDIA due to to missing GROUP-ID attribute");
        continue;
      }
      mediaGroups[attribs["GROUP-ID"]].push_back(attribs);
    }
    else if (tagName == "#EXT-X-STREAM-INF")
    {
      //! @todo: If CODECS value is not present, get StreamReps from stream program section
      // #EXT-X-STREAM-INF:BANDWIDTH=263851,CODECS="mp4a.40.2, avc1.4d400d",RESOLUTION=416x234,AUDIO="bipbop_audio",SUBTITLES="subs"

      auto attribs = ParseTagAttributes(tagValue);

      if (!STRING::KeyExists(attribs, "BANDWIDTH"))
      {
        LOG::LogF(LOGERROR, "Cannot parse EXT-X-STREAM-INF due to to missing bandwidth attribute (%s)",
                  tagValue.c_str());
        continue;
      }

      // Try read on the next stream line, to get the playlist URL address
      std::string playlistUrl;
      if (STRING::GetLine(streamData, line) && !line.empty() && line[0] != '#')
      {
        playlistUrl = line;
        if (URL::IsUrlRelative(playlistUrl))
          playlistUrl = URL::Join(base_url_, playlistUrl);
      }
      else
      {
        // Malformed line, rollback stream to previous line position
        streamData.seekg(currentStreamPos);
        continue;
      }

      // Try determine the type of stream from codecs
      StreamType streamType = StreamType::NOTYPE;
      std::string codecVideo;
      std::string codecAudio;

      if (STRING::KeyExists(attribs, "CODECS"))
      {
        std::string codecs = attribs["CODECS"];
        codecVideo = GetVideoCodec(codecs);
        codecAudio = GetAudioCodec(codecs);

        if (!codecVideo.empty()) // Audio/Video stream
          streamType = StreamType::VIDEO;
        else if (!codecAudio.empty()) // Audio only stream
          streamType = StreamType::AUDIO;
      }
      else
      {
        if (STRING::KeyExists(attribs, "RESOLUTION"))
        {
          LOG::LogF(
              LOGDEBUG,
              "CODECS attribute missing in the EXT-X-STREAM-INF variant, assumed as video stream");
          streamType = StreamType::VIDEO;
          codecVideo = CODEC::FOURCC_H264;
          if (codecAudio.empty())
            codecAudio = CODEC::FOURCC_MP4A;
        }
        else
          LOG::LogF(LOGERROR, "The EXT-X-STREAM-INF variant does not have enough info to "
                              "determine the stream type");
      }

      // If there is a subtitle group specified add each one
      if (STRING::KeyExists(attribs, "SUBTITLES"))
      {
        std::string groupId = attribs["SUBTITLES"];

        if (!STRING::KeyExists(mediaGroupParsed, groupId))
        {
          for (auto& groupAttribs : mediaGroups[groupId])
          {
            auto newAdpSet = CAdaptationSet::MakeUniquePtr(period.get());
            auto newRepr = CRepresentation::MakeUniquePtr(newAdpSet.get());
            // Use WebVTT as default subtitle codec
            if (ParseMediaGroup(newAdpSet, newRepr, groupAttribs, CODEC::FOURCC_WVTT))
            {
              newAdpSet->AddRepresentation(newRepr);
              period->AddAdaptationSet(newAdpSet);
            }
          }
          mediaGroupParsed.emplace(groupId);
        }
      }

      // If there is no audio group specified, we assume audio is included to video stream
      if (!STRING::KeyExists(attribs, "AUDIO") && streamType == StreamType::VIDEO)
      {
        period->m_includedStreamType |= 1U << static_cast<int>(StreamType::AUDIO);
        createDummyAudioRepr = true;
      }

      // If there is an audio group specified add each one
      if (STRING::KeyExists(attribs, "AUDIO") && streamType != StreamType::AUDIO)
      {
        std::string groupId = attribs["AUDIO"];
        if (!STRING::KeyExists(mediaGroupParsed, groupId))
        {
          // Several EXT-X-MEDIA tags may exist with the same group id,
          // usually should be with same codec and attributes but different URI and channels
          for (auto& groupAttribs : mediaGroups[groupId])
          {
            auto newAdpSet = CAdaptationSet::MakeUniquePtr(period.get());
            auto newRepr = CRepresentation::MakeUniquePtr(newAdpSet.get());
            if (ParseMediaGroup(newAdpSet, newRepr, groupAttribs, codecAudio))
            {
              if (newRepr->GetSourceUrl().empty()) // EXT-X-MEDIA without URI
              {
                newRepr->SetIsIncludedStream(true);
                period->m_includedStreamType |= 1U << static_cast<int>(streamType);
              }
              // Ensure that we dont have already an existing adaptation set with same attributes,
              // usually should happens only when we have more EXT-X-MEDIA without URI (audio included to video)
              // and we need to keep just one
              CAdaptationSet* foundAdpSet =
                  CAdaptationSet::FindMergeable(period->GetAdaptationSets(), newAdpSet.get());

              if (foundAdpSet)
              {
                if (newRepr->GetSourceUrl().empty())
                  continue; // Already added included audio, so skip it

                newRepr->SetParent(foundAdpSet);
                foundAdpSet->AddRepresentation(newRepr);
              }
              else
              {
                newAdpSet->AddRepresentation(newRepr);
                period->AddAdaptationSet(newAdpSet);
              }
            }
          }
          mediaGroupParsed.emplace(groupId);
        }
      }

      if (streamType == StreamType::AUDIO)
      {
        auto newAdpSet = CAdaptationSet::MakeUniquePtr(period.get());
        auto newRepr = CRepresentation::MakeUniquePtr(newAdpSet.get());

        if (STRING::KeyExists(attribs, "AUDIO"))
        {
          // Get info from specified group
          std::string groupId = attribs["AUDIO"];

          if (!STRING::KeyExists(mediaGroups, groupId))
          {
            LOG::LogF(LOGERROR, "Cannot create stream due to missing group id \"%s\"",
                      groupId.c_str());
            continue;
          }

          auto& groupAttribs = mediaGroups[groupId][0];
          if (!ParseMediaGroup(newAdpSet, newRepr, groupAttribs, codecAudio))
            continue;
        }
        else
        {
          // Constructs generic info
          newAdpSet->SetStreamType(StreamType::AUDIO);
          newAdpSet->SetLanguage("unk"); // Unknown
          newAdpSet->AddCodecs(codecAudio);
          newRepr->AddCodecs(codecAudio);
          newRepr->SetAudioChannels(2);
          newRepr->SetTimescale(1000000);
          newRepr->assured_buffer_duration_ = m_settings.m_bufferAssuredDuration;
          newRepr->max_buffer_duration_ = m_settings.m_bufferMaxDuration;
          newRepr->SetScaling();
        }

        newRepr->SetBandwidth(STRING::ToUint32(attribs["BANDWIDTH"]));
        newRepr->SetSourceUrl(playlistUrl);

        // Check if it is mergeable with an existing adp set
        CAdaptationSet* foundAdpSet =
            CAdaptationSet::FindMergeable(period->GetAdaptationSets(), newAdpSet.get());
        if (foundAdpSet) // Reuse existing adp set
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
      else if (streamType == StreamType::VIDEO)
      {
        std::string codecStr = codecVideo.substr(0, codecVideo.find('.')); // Get fourcc only
        // Find existing adaptation set with same codec ...
        CAdaptationSet* adpSet = CAdaptationSet::FindByCodec(period->GetAdaptationSets(), codecStr);
        if (!adpSet) // ... or create a new one
        {
          auto newAdpSet = CAdaptationSet::MakeUniquePtr(period.get());
          newAdpSet->SetStreamType(streamType);
          newAdpSet->AddCodecs(codecStr);
          adpSet = newAdpSet.get();
          period->AddAdaptationSet(newAdpSet);
        }

        auto repr = CRepresentation::MakeUniquePtr(adpSet);
        repr->SetTimescale(1000000);
        repr->AddCodecs(codecVideo);
        repr->SetBandwidth(STRING::ToUint32(attribs["BANDWIDTH"]));

        if (STRING::KeyExists(attribs, "RESOLUTION"))
        {
          int resWidth{0};
          int resHeight{0};
          ParseResolution(resWidth, resHeight, attribs["RESOLUTION"]);
          repr->SetResWidth(resWidth);
          repr->SetResHeight(resHeight);
        }

        if (STRING::KeyExists(attribs, "FRAME-RATE"))
        {
          double frameRate = STRING::ToFloat(attribs["FRAME-RATE"]);
          if (frameRate == 0)
          {
            LOG::LogF(LOGWARNING, "Wrong FRAME-RATE attribute, fallback to 60 fps");
            frameRate = 60.0f;
          }
          repr->SetFrameRate(static_cast<uint32_t>(frameRate * 1000));
          repr->SetFrameRateScale(1000);
        }

        repr->assured_buffer_duration_ = m_settings.m_bufferAssuredDuration;
        repr->max_buffer_duration_ = m_settings.m_bufferMaxDuration;
        repr->SetScaling();
        repr->SetSourceUrl(playlistUrl);
        adpSet->AddRepresentation(repr);
      }
      else
      {
        LOG::LogF(LOGWARNING, "Unhandled EXT-X-STREAM-INF: %s", tagValue.c_str());
      }
    }
    else if (tagName == "#EXTINF")
    {
      // This is a media playlist (not a master playlist with multi-bitrate playlist)

      //! @todo: here we are add some fake data, and we are downloading two times the same manifest
      //! because the current parser code is splitted on two parts and managed from different code,
      //! to solve this situation a rework is needed, where we can have a seletable parsing method
      auto newAdpSet = CAdaptationSet::MakeUniquePtr(period.get());
      newAdpSet->SetStreamType(StreamType::VIDEO);

      auto repr = CRepresentation::MakeUniquePtr(newAdpSet.get());
      repr->SetTimescale(1000000);
      repr->SetSourceUrl(manifest_url_);
      repr->AddCodecs(CODEC::FOURCC_H264);

      repr->assured_buffer_duration_ = m_settings.m_bufferAssuredDuration;
      repr->max_buffer_duration_ = m_settings.m_bufferMaxDuration;

      repr->SetScaling();

      newAdpSet->AddRepresentation(repr);
      period->AddAdaptationSet(newAdpSet);

      // We assume audio is included
      period->m_includedStreamType |= 1U << static_cast<int>(StreamType::AUDIO);
      createDummyAudioRepr = true;
      break;
    }
    else if (tagName == "#EXT-X-SESSION-KEY")
    {
      auto attribs = ParseTagAttributes(tagValue);

      switch (ProcessEncryption(base_url_, attribs))
      {
        case EncryptionType::NOT_SUPPORTED:
          return false;
        case EncryptionType::AES128:
        case EncryptionType::WIDEVINE:
          // #EXT-X-SESSION-KEY is meant for preparing DRM without
          // loading sub-playlist. As long our workflow is serial, we
          // don't profite and therefore do not any action.
          break;
        case EncryptionType::UNKNOWN:
          LOG::LogF(LOGWARNING, "Unknown encryption type");
          break;
        default:
          break;
      }
    }
  }

  if (!isExtM3Uformat)
  {
    LOG::LogF(LOGERROR, "Non-compliant HLS manifest, #EXTM3U tag not found.");
    return false;
  }

  if (createDummyAudioRepr)
  {
    // We may need to create the Default / Dummy audio representation

    auto newAdpSet = CAdaptationSet::MakeUniquePtr(period.get());
    newAdpSet->SetStreamType(StreamType::AUDIO);
    newAdpSet->SetContainerType(ContainerType::MP4);
    newAdpSet->SetLanguage("unk"); // Unknown

    auto repr = CRepresentation::MakeUniquePtr(newAdpSet.get());
    repr->SetTimescale(1000000);

    // Try to get the codecs from first representation
    std::set<std::string> codecs{CODEC::FOURCC_MP4A};
    auto& adpSets = period->GetAdaptationSets();
    if (!adpSets.empty())
    {
      auto& reprs = adpSets[0]->GetRepresentations();
      if (!reprs.empty())
        codecs = reprs[0]->GetCodecs();
    }
    repr->AddCodecs(codecs);
    repr->SetAudioChannels(2);
    repr->SetIsIncludedStream(true);

    repr->assured_buffer_duration_ = m_settings.m_bufferAssuredDuration;
    repr->max_buffer_duration_ = m_settings.m_bufferMaxDuration;

    repr->SetScaling();

    newAdpSet->AddRepresentation(repr);
    period->AddAdaptationSet(newAdpSet);
  }

  // Set Live as default
  m_isLive = true;

  m_periods.push_back(std::move(period));

  return true;
}

PLAYLIST::EncryptionType adaptive::CHLSTree::ProcessEncryption(
    std::string_view baseUrl, std::map<std::string, std::string>& attribs)
{
  std::string encryptMethod = attribs["METHOD"];

  // NO ENCRYPTION
  if (encryptMethod == "NONE")
  {
    m_currentPssh.clear();

    return EncryptionType::CLEAR;
  }

  // AES-128
  if (encryptMethod == "AES-128" && !attribs["URI"].empty())
  {
    m_currentPssh = attribs["URI"];
    if (URL::IsUrlRelative(m_currentPssh))
      m_currentPssh = URL::Join(baseUrl.data(), m_currentPssh);

    m_currentIV = m_decrypter->convertIV(attribs["IV"]);

    return EncryptionType::AES128;
  }

  // WIDEVINE
  if (STRING::CompareNoCase(attribs["KEYFORMAT"],
                            "urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed") &&
      !attribs["URI"].empty())
  {
    if (!attribs["KEYID"].empty())
    {
      std::string keyid = attribs["KEYID"].substr(2);
      const char* defaultKID = keyid.c_str();
      m_currentDefaultKID.resize(16);
      for (unsigned int i(0); i < 16; ++i)
      {
        m_currentDefaultKID[i] = STRING::ToHexNibble(*defaultKID) << 4;
        ++defaultKID;
        m_currentDefaultKID[i] |= STRING::ToHexNibble(*defaultKID);
        ++defaultKID;
      }
    }

    m_currentPssh = attribs["URI"].substr(23);
    // Try to get KID from pssh, we assume len+'pssh'+version(0)+systemid+lenkid+kid
    if (m_currentDefaultKID.empty() && m_currentPssh.size() == 68)
    {
      std::string decPssh{BASE64::Decode(m_currentPssh)};
      if (decPssh.size() == 50)
        m_currentDefaultKID = decPssh.substr(34, 16);
    }
    if (encryptMethod == "SAMPLE-AES-CTR")
      m_cryptoMode = CryptoMode::AES_CTR;
    else if (encryptMethod == "SAMPLE-AES")
      m_cryptoMode = CryptoMode::AES_CBC;

    return EncryptionType::WIDEVINE;
  }

  // KNOWN UNSUPPORTED
  if (STRING::CompareNoCase(attribs["KEYFORMAT"], "com.apple.streamingkeydelivery"))
  {
    LOG::LogF(LOGDEBUG, "Keyformat %s not supported", attribs["KEYFORMAT"].c_str());
    return EncryptionType::NOT_SUPPORTED;
  }

  return EncryptionType::UNKNOWN;
}

bool adaptive::CHLSTree::ParseMediaGroup(std::unique_ptr<PLAYLIST::CAdaptationSet>& adpSet,
                                         std::unique_ptr<PLAYLIST::CRepresentation>& repr,
                                         std::map<std::string, std::string> attribs,
                                         const std::string& codecStr)
{
  StreamType streamType = StreamType::NOTYPE;
  if (attribs["TYPE"] == "AUDIO")
    streamType = StreamType::AUDIO;
  else if (attribs["TYPE"] == "SUBTITLES")
    streamType = StreamType::SUBTITLE;
  else
    return false;

  adpSet->SetStreamType(streamType);
  adpSet->SetLanguage(attribs["LANGUAGE"].empty() ? "unk" : attribs["LANGUAGE"]);
  adpSet->SetName(attribs["NAME"]);
  adpSet->SetIsDefault(attribs["DEFAULT"] == "YES");
  adpSet->SetIsForced(attribs["FORCED"] == "YES");
  adpSet->AddCodecs(codecStr);

  if (STRING::KeyExists(attribs, "CHARACTERISTICS"))
  {
    std::string ch = attribs["CHARACTERISTICS"];
    if (STRING::Contains(ch, "public.accessibility.transcribes-spoken-dialog") ||
        STRING::Contains(ch, "public.accessibility.describes-music-and-sound") ||
        STRING::Contains(ch, "public.accessibility.describes-video"))
    {
      adpSet->SetIsImpaired(true);
    }
  }

  repr->AddCodecs(codecStr);
  repr->SetTimescale(1000000);

  if (STRING::KeyExists(attribs, "URI"))
  {
    std::string uri = attribs["URI"];
    if (URL::IsUrlRelative(uri))
      uri = URL::Join(base_url_, uri);

    repr->SetSourceUrl(uri);
  }

  if (streamType == StreamType::AUDIO)
  {
    repr->SetAudioChannels(STRING::ToUint32(attribs["CHANNELS"], 2));
    // Copy channels in the adptation set to help distinguish it from other groups
    adpSet->SetAudioChannels(repr->GetAudioChannels());
  }

  repr->assured_buffer_duration_ = m_settings.m_bufferAssuredDuration;
  repr->max_buffer_duration_ = m_settings.m_bufferMaxDuration;

  repr->SetScaling();

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
