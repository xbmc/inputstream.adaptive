/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Session.h"

#include "CompKodiProps.h"
#include "CompSettings.h"
#include "SrvBroker.h"
#include "aes_decrypter.h"
#include "common/AdaptiveDecrypter.h"
#include "common/AdaptiveTreeFactory.h"
#include "common/Chooser.h"
#include "common/ReprSelector.h"
#include "decrypters/DrmFactory.h"
#include "decrypters/Helpers.h"
#include "samplereader/SampleReaderFactory.h"
#include "utils/CurlUtils.h"
#include "utils/GUIUtils.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <algorithm>
#include <cassert>

using namespace adaptive;
using namespace PLAYLIST;
using namespace SESSION;
using namespace UTILS;

namespace
{
constexpr const char* CHAPTER_NAME_UNKNOWN = "[Unknown]";

// \brief Make an unique ID based on period index and stream index.
// \param periodIndex The max value accepted by period index is uint16_t type max value.
// \param streamIndex The max value accepted by stream index is 2047.
uint32_t MakeUniqueId(uint16_t periodIndex, uint16_t streamIndex)
{
  assert(streamIndex <= 2047 && "streamIndex exceeds maximum for 11 bits (2047)");
  return (static_cast<uint32_t>(periodIndex) << 11) | (streamIndex & 0x7FFu);
}
} // unnamed namespace

SESSION::CSession::~CSession()
{
  LOG::Log(LOGDEBUG, "CSession::~CSession()");
  DeleteStreams();
  m_drmEngine.Dispose();

  if (m_adaptiveTree)
  {
    m_adaptiveTree->Uninitialize();
    delete m_adaptiveTree;
    m_adaptiveTree = nullptr;
  }

  delete m_reprChooser;
  m_reprChooser = nullptr;
}

void SESSION::CSession::DeleteStreams()
{
  LOG::Log(LOGDEBUG, "CSession::DeleteStreams()");
  m_streams.clear();
  m_timingStream = nullptr;
}

/*----------------------------------------------------------------------
|   initialize
+---------------------------------------------------------------------*/

SResult SESSION::CSession::Initialize(std::string manifestUrl)
{
  m_reprChooser = CHOOSER::CreateRepresentationChooser();

  switch (CSrvBroker::GetSettings().GetMediaType())
  {
    case 1:
      m_mediaTypeMask = static_cast<uint8_t>(1U) << static_cast<int>(StreamType::AUDIO);
      break;
    case 2:
      m_mediaTypeMask = static_cast<uint8_t>(1U) << static_cast<int>(StreamType::VIDEO);
      break;
    case 3:
      m_mediaTypeMask = (static_cast<uint8_t>(1U) << static_cast<int>(StreamType::VIDEO)) |
                        (static_cast<uint8_t>(1U) << static_cast<int>(StreamType::SUBTITLE));
      break;
    default:
      m_mediaTypeMask = static_cast<uint8_t>(~0);
  }

  auto& kodiProps = CSrvBroker::GetKodiProps();
  std::map<std::string, std::string> manifestHeaders = kodiProps.GetManifestHeaders();

  m_drmEngine.Initialize();

  DRM::DRMSession session;
  // Pre-initialize the DRM allow to generate the challenge and session ID data
  // used to make licensed manifest requests
  if (m_drmEngine.PreInitializeDRM(session))
  {
    // The following are custom headers that must be handled through a proxy server
    manifestHeaders["challengeB64"] = STRING::URLEncode(session.challenge);
    manifestHeaders["sessionId"] = session.id;
  }

  URL::RemovePipePart(manifestUrl); // No pipe char uses, must be used Kodi properties only

  URL::AppendParameters(manifestUrl, kodiProps.GetManifestParams());

  CURL::HTTPResponse manifestResp;
  if (!CURL::DownloadFile(manifestUrl, manifestHeaders, {"etag", "last-modified"}, manifestResp))
    return SResult::Error(GUI::GetLocalizedString(30307));

  // The download speed with small file sizes is not accurate, we should download at least 512Kb
  // to have a sufficient acceptable value to calculate the bandwidth,
  // then to have a better speed value we apply following proportion hack.
  // This does not happen when you play with webbrowser because can obtain the connection speed.
  static const size_t minSize{512 * 1024};
  if (manifestResp.dataSize < minSize)
    manifestResp.downloadSpeed = (manifestResp.downloadSpeed / manifestResp.dataSize) * minSize;

  // We set the download speed to calculate the initial network bandwidth
  m_reprChooser->SetDownloadSpeed(manifestResp.downloadSpeed);

  m_adaptiveTree = PLAYLIST_FACTORY::CreateAdaptiveTree(manifestResp);
  if (!m_adaptiveTree)
  {
    LOG::LogF(LOGERROR, "Unable to determine type of manifest file.");
    return SResult::Error(GUI::GetLocalizedString(30308));
  }

  m_adaptiveTree->Configure(m_reprChooser, kodiProps.GetManifestUpdParams());

  if (!m_adaptiveTree->Open(manifestResp.effectiveUrl, manifestResp.headers, manifestResp.data))
  {
    LOG::LogF(LOGERROR, "Cannot parse the manifest file.");
    return SResult::Error(GUI::GetLocalizedString(30308));
  }

  m_adaptiveTree->PostOpen();
  m_reprChooser->PostInit();

  CSrvBroker::GetInstance()->InitStage2(m_adaptiveTree);

  InitializePeriod();

  return SResultCode::OK;
}

bool SESSION::CSession::CheckPlayableStreams(PLAYLIST::CPeriod* period)
{
  auto& kodiPropCfg = CSrvBroker::GetKodiProps().GetConfig();

  /*! @todo: Code commented, see todo below about: "Secure path on audio stream is not implemented"
   *
  if (kodiPropCfg.resolutionLimit == 0 &&
      kodiPropCfg.hdcpCheck == ADP::KODI_PROPS::HdcpCheckType::DEFAULT)
  {
    return;
  }
  */

  for (auto& adp : period->GetAdaptationSets())
  {
    if (adp->GetStreamType() == StreamType::NOTYPE)
      continue;

    for (auto& repr : adp->GetRepresentations())
    {
      //! @todo: Code changed, see todo below about: "Secure path on audio stream is not implemented"
      // if (kodiPropCfg.hdcpCheck == ADP::KODI_PROPS::HdcpCheckType::LICENSE &&
      //     !m_adaptiveTree->IsReqPrepareStream())
      if (!m_adaptiveTree->IsReqPrepareStream())
      {
        // The LICENSE method assume that a service provide in the license response the HDCP parameters
        // currently a comparison is made between the license HDCP values and the one provided by the manifest.
        // To do it you have to initialize the DRM to handle all streams,
        // this could be expensive to do since you can request a license for each stream
        if (!repr->DrmInfos().empty())
        {
          kodi::addon::InputstreamInfo isInfo;
          DRM::DRMInfo initDrmInfo;

          DRM::DRMMediaType drmMediaType{DRM::DRMMediaType::UNKNOWN};
          const StreamType sType = adp->GetStreamType();

          if (sType == StreamType::VIDEO || sType == StreamType::VIDEO_AUDIO)
            drmMediaType = DRM::DRMMediaType::VIDEO;
          else if (sType == StreamType::AUDIO)
            drmMediaType = DRM::DRMMediaType::AUDIO;
          else
          {
            LOG::LogF(LOGWARNING, "Stream media type \"%i\" is not supported by the DRM engine",
                      static_cast<int>(sType));
            continue;
          }

          if (m_drmEngine.InitializeSession(repr->DrmInfos(), drmMediaType,
                                            period->IsSecureDecodeNeeded(), isInfo, repr.get(),
                                            adp.get(), false, initDrmInfo))
          {
            if (!isInfo.GetCryptoSession().GetSessionId().empty())
            {
              const auto session = m_drmEngine.GetSession(isInfo.GetCryptoSession().GetSessionId(),
                                                          initDrmInfo.defaultKid);
              if (session)
              {
                //! @todo: HACK REQUIRED BECAUSE ---> Secure path on audio stream is not implemented for CDM Widevine ONLY (non-android) <---
                //! since audio streams that require Secure path decoder cannot be played
                //! we have no way to distinguish which ones they are other than to do a KID test with the DRM for each stream,
                //! this is an expensive method that could open many DRM sessions which will not be unused for playback.
                //! When in a future this will be implemented, all this code should be cleanup and removed
                //! and then leave it to CInputStreamAdaptive::OpenStream -> PrepareStream
                //! the task to initialize DRM session only to the requested streams.
                //! It can be tested e.g. with some Am@zon videos
                auto& caps = session->capabilities;

                if (!session->drm->IsSecureDecoderAudioSupported() &&
                    adp->GetStreamType() == StreamType::AUDIO &&
                    caps.flags & DRM::DecrypterCapabilites::SSD_SECURE_PATH)
                {
                  LOG::Log(LOGWARNING,
                           "Disabled stream repr ID \"%s\", AdpSet ID \"%s\", "
                           "Secure path decoder on audio stream is not supported",
                           repr->GetId().c_str(), adp->GetId().c_str());
                  repr->isPlayable = false;
                  continue;
                }

                // Note to HDCP check:
                // HDCP check should be done by the DRM where in case of problems should block key's
                // for example with Widevine you will get "output-restricted" to key status
                // the following it's an additional check for custom manifest's
                if (kodiPropCfg.hdcpCheck == ADP::KODI_PROPS::HdcpCheckType::LICENSE)
                {
                  if (repr->GetHdcpVersion() > caps.hdcpVersion ||
                      (caps.hdcpLimit > 0 && repr->GetWidth() * repr->GetHeight() > caps.hdcpLimit))
                  {
                    LOG::Log(
                        LOGWARNING,
                        "Disabled stream repr ID \"%s\", AdpSet ID \"%s\", as not HDCP compliant",
                        repr->GetId().c_str(), adp->GetId().c_str());
                    repr->isPlayable = false;
                    continue;
                  }

                }
              }
            }
          }
          else
          {
            if (m_drmEngine.GetStatus() == DRM::EngineStatus::DRM_ERROR ||
                m_drmEngine.GetStatus() == DRM::EngineStatus::DECRYPTER_ERROR)
            {
              return false; // return here, a bad status dont allow you to play streams
            }
          }
        }
      }
      // Limit streams resolutions available on the manifest,
      // some video services apply protections that limit playable resolutions,
      // this is a kind of workaround to avoid playback errors or black screen,
      //! @todo: these situations should be better handled in the DRM/session implementation
      if (kodiPropCfg.resolutionLimit > 0)
      {
        if (repr->GetWidth() * repr->GetHeight() > kodiPropCfg.resolutionLimit)
        {
          LOG::Log(
              LOGWARNING,
              "Disabled stream repr ID \"%s\", AdpSet ID \"%s\", it exceeds the resolution limits",
              repr->GetId().c_str(), adp->GetId().c_str());
          repr->isPlayable = false;
        }
      }
    }
  }

  return true;
}

void SESSION::CSession::InitializePeriod()
{
  if (m_adaptiveTree->IsChangingPeriod())
  {
    // Complete the transition into the new period
    m_adaptiveTree->m_currentPeriod = m_adaptiveTree->m_nextPeriod;
    m_adaptiveTree->OnPeriodChange();
  }

  // Clean to create new SESSION::STREAM objects. One for each AdaptationSet/Representation
  m_streams.clear();

  CPeriod* currPeriod = m_adaptiveTree->m_currentPeriod;
  if (!currPeriod)
  {
    LOG::LogF(LOGFATAL, "No period found on AdaptiveTree class");
    return;
  }

  m_chapterStartTime = GetChapterStartTime();

  // Note: this could initialize DRM on all streams right here, instead of CInputStreamAdaptive::OpenStream
  if (!CheckPlayableStreams(currPeriod))
    return;

  CHOOSER::StreamSelection streamSelectionMode = m_reprChooser->GetStreamSelectionMode();
  //! @todo: GetAudioLangOrig property should be reworked to allow override or set
  //! manifest a/v and subtitles streams attributes such as default/original etc..
  //! since Kodi stream flags dont have always the same meaning of manifest attributes
  //! and some video services dont follow exactly the specs so can lead to wrong Kodi flags sets.
  //! An idea is add/move these override of attributes on post manifest parsing.
  std::string audioLanguageOrig = CSrvBroker::GetKodiProps().GetAudioLangOrig();

  // For multi-codec manifests, determine which codec to use by default,
  // then choose the appropriate AdaptationSet. It may also depend on the Chooser behavior.
  const CAdaptationSet* defVideoAdpSet =
      m_reprChooser->GetPreferredVideoAdpSet(currPeriod, DetermineDefaultAdpSet(currPeriod));

  const uint16_t periodIndex{currPeriod->GetIndex()};
  uint16_t streamIndex{0}; // Stream index in the current period

  for (auto& adp : currPeriod->GetAdaptationSets())
  {
    if (adp->GetRepresentations().empty())
      continue;

    if (adp->GetStreamType() == StreamType::NOTYPE)
    {
      LOG::LogF(LOGDEBUG, "Skipped streams on adaptation set id \"%s\" due to unsupported/unknown type",
                adp->GetId().c_str());
      continue;
    }

    bool isManualStreamSelection;
    if (adp->GetStreamType() == StreamType::VIDEO)
      isManualStreamSelection = streamSelectionMode != CHOOSER::StreamSelection::AUTO;
    else
      isManualStreamSelection = streamSelectionMode == CHOOSER::StreamSelection::MANUAL;

    const bool isDefaultAdpSet{adp.get() == defVideoAdpSet};

    // Get the default initial stream repr. based on "adaptive repr. chooser"
    CRepresentation* defaultRepr{m_reprChooser->GetRepresentation(adp.get())};
    if (isDefaultAdpSet)
      m_reprChooser->LogDetails(defaultRepr);

    // Helper lambda method to add a stream
    auto AddStreamRepr = [&](PLAYLIST::CRepresentation* repr) -> bool
    {
      if (streamIndex >= INPUTSTREAM_MAX_STREAM_COUNT)
      {
        LOG::LogF(
            LOGWARNING,
            "Exceeded the maximum limit of %i streams. Some streams will be excluded from playback",
            INPUTSTREAM_MAX_STREAM_COUNT);
        return false;
      }

      const uint32_t uniqueId = MakeUniqueId(periodIndex, streamIndex);
      const bool isDefaultVideoRepr{isDefaultAdpSet && repr == defaultRepr};

      AddStream(adp.get(), repr, isDefaultVideoRepr, uniqueId, audioLanguageOrig);
      ++streamIndex;
      return true;
    };

    if (isManualStreamSelection)
    {
      // Add all stream representations
      for (auto& currentRepr : adp->GetRepresentations())
      {
        if (!currentRepr->isPlayable)
          continue;

        if (!AddStreamRepr(currentRepr.get()))
          break;
      }
    }
    else
    {
      // Add the default stream representation only
      if (!defaultRepr->isPlayable)
        continue;

      if (!AddStreamRepr(defaultRepr))
        break;
    }
  }

  if (m_streams.empty())
  {
    LOG::LogF(LOGERROR,
              "No stream can be played, common causes: resolution limits or HDCP problem");
    GUI::ErrorDialog(GUI::GetLocalizedString(30309));
  }

  // Ensure that video streams are always placed before all other types
  std::stable_sort(m_streams.begin(), m_streams.end(),
                   [](const std::shared_ptr<CStream>& a, const std::shared_ptr<CStream>& b)
                   {
                     const bool aIsVideo = a && a->m_info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO;
                     const bool bIsVideo = b && b->m_info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO;
                     return aIsVideo && !bIsVideo;
                   });
}

void SESSION::CSession::AddStream(PLAYLIST::CAdaptationSet* adp,
                                  PLAYLIST::CRepresentation* initialRepr,
                                  bool isDefaultVideoRepr,
                                  uint32_t uniqueId,
                                  std::string_view audioLanguageOrig)
{
  m_streams.push_back(std::make_shared<CStream>(m_adaptiveTree, adp, initialRepr));

  CStream& stream{*m_streams.back()};

  uint32_t flags{INPUTSTREAM_FLAG_NONE};
  stream.m_info.SetName(adp->GetName());

  switch (adp->GetStreamType())
  {
    case StreamType::VIDEO:
    {
      stream.m_info.SetStreamType(INPUTSTREAM_TYPE_VIDEO);
      if (isDefaultVideoRepr)
        flags |= INPUTSTREAM_FLAG_DEFAULT;
      break;
    }
    case StreamType::AUDIO:
    {
      stream.m_info.SetStreamType(INPUTSTREAM_TYPE_AUDIO);
      if (adp->IsImpaired())
        flags |= INPUTSTREAM_FLAG_VISUAL_IMPAIRED;
      if (adp->IsDefault())
        flags |= INPUTSTREAM_FLAG_DEFAULT;
      if (adp->IsOriginal() || (!audioLanguageOrig.empty() &&
                                adp->GetLanguage() == audioLanguageOrig))
      {
        flags |= INPUTSTREAM_FLAG_ORIGINAL;
      }
      break;
    }
    case StreamType::SUBTITLE:
    {
      stream.m_info.SetStreamType(INPUTSTREAM_TYPE_SUBTITLE);
      if (adp->IsImpaired())
        flags |= INPUTSTREAM_FLAG_HEARING_IMPAIRED;
      if (adp->IsForced())
        flags |= INPUTSTREAM_FLAG_FORCED;
      if (adp->IsDefault())
        flags |= INPUTSTREAM_FLAG_DEFAULT;
      break;
    }
    default:
      break;
  }

  stream.m_info.SetFlags(flags);
  stream.m_info.SetPhysicalIndex(uniqueId);

  std::string langCode = adp->GetLanguage();
  if (langCode.empty() && stream.m_info.GetStreamType() != INPUTSTREAM_TYPE_VIDEO)
    langCode = LANG_CODE::UNDETERMINED;

  stream.m_info.SetLanguage(langCode);

  stream.m_info.ClearExtraData();
  stream.m_info.SetFeatures(0);

  stream.m_adStream.set_observer(dynamic_cast<adaptive::AdaptiveStreamObserver*>(this));

  UpdateStream(stream);
}

void SESSION::CSession::UpdateStream(CStream& stream)
{
  // On this method we set stream info provided by manifest parsing, but these info could be
  // changed by sample readers just before the start of playback by using GetInformation() methods
  const StreamType streamType = stream.m_adStream.getAdaptationSet()->GetStreamType();
  CRepresentation* rep{stream.m_adStream.getRepresentation()};

  if (rep->GetContainerType() == ContainerType::INVALID)
  {
    LOG::LogF(LOGERROR, "Container type not valid on stream representation ID: %s",
              rep->GetId().c_str());
    stream.m_isValid = false;
    return;
  }

  stream.m_info.SetExtraData(nullptr, 0);

  if (!rep->GetCodecPrivateData().empty())
    stream.m_info.SetExtraData(rep->GetCodecPrivateData());

  stream.m_info.SetCodecFourCC(0);
  stream.m_info.SetBitRate(rep->GetBandwidth());
  const std::set<std::string>& codecs = rep->GetCodecs();

  // Original codec string
  std::string codecStr;

  if (streamType == StreamType::VIDEO)
  {
    stream.m_info.SetWidth(static_cast<uint32_t>(rep->GetWidth()));
    stream.m_info.SetHeight(static_cast<uint32_t>(rep->GetHeight()));
    stream.m_info.SetAspect(rep->GetAspectRatio());

    if (stream.m_info.GetAspect() == 0.0f && stream.m_info.GetHeight())
      stream.m_info.SetAspect(static_cast<float>(stream.m_info.GetWidth()) /
                              stream.m_info.GetHeight());

    stream.m_info.SetFpsRate(rep->GetFrameRate());
    stream.m_info.SetFpsScale(rep->GetFrameRateScale());

    stream.m_info.SetColorSpace(INPUTSTREAM_COLORSPACE_UNSPECIFIED);
    stream.m_info.SetColorRange(INPUTSTREAM_COLORRANGE_UNKNOWN);
    stream.m_info.SetColorPrimaries(INPUTSTREAM_COLORPRIMARY_UNSPECIFIED);

    ColorTRC colorTRC = rep->GetColorTRC();
    if (colorTRC == ColorTRC::SMPTE2084)
      stream.m_info.SetColorTransferCharacteristic(INPUTSTREAM_COLORTRC_SMPTE2084);
    else if (colorTRC == ColorTRC::ARIB_STD_B67)
      stream.m_info.SetColorTransferCharacteristic(INPUTSTREAM_COLORTRC_ARIB_STD_B67);
    else
      stream.m_info.SetColorTransferCharacteristic(INPUTSTREAM_COLORTRC_UNSPECIFIED);

    if (CODEC::Contains(codecs, CODEC::FOURCC_AVC_, codecStr) ||
        CODEC::Contains(codecs, CODEC::FOURCC_H264, codecStr))
    {
      stream.m_info.SetCodecName(CODEC::NAME_H264);

      if (STRING::Contains(codecStr, CODEC::FOURCC_AVC1))
        stream.m_info.SetCodecFourCC(CODEC::MakeFourCC(CODEC::FOURCC_AVC1));
      else if (STRING::Contains(codecStr, CODEC::FOURCC_AVC2))
        stream.m_info.SetCodecFourCC(CODEC::MakeFourCC(CODEC::FOURCC_AVC2));
      else if (STRING::Contains(codecStr, CODEC::FOURCC_AVC3))
        stream.m_info.SetCodecFourCC(CODEC::MakeFourCC(CODEC::FOURCC_AVC3));
      else if (STRING::Contains(codecStr, CODEC::FOURCC_AVC4))
        stream.m_info.SetCodecFourCC(CODEC::MakeFourCC(CODEC::FOURCC_AVC4));
    }
    else if (CODEC::Contains(codecs, CODEC::FOURCC_HEVC, codecStr))
      stream.m_info.SetCodecName(CODEC::NAME_HEVC);
    else if (CODEC::Contains(codecs, CODEC::FOURCC_HVC1, codecStr))
    {
      stream.m_info.SetCodecName(CODEC::NAME_HEVC);
      stream.m_info.SetCodecFourCC(CODEC::MakeFourCC(CODEC::FOURCC_HVC1));
    }
    else if (CODEC::Contains(codecs, CODEC::FOURCC_DVH1, codecStr))
    {
      stream.m_info.SetCodecName(CODEC::NAME_HEVC);
      stream.m_info.SetCodecFourCC(CODEC::MakeFourCC(CODEC::FOURCC_DVH1));
    }
    else if (CODEC::Contains(codecs, CODEC::FOURCC_HEV1, codecStr))
    {
      stream.m_info.SetCodecName(CODEC::NAME_HEVC);
      stream.m_info.SetCodecFourCC(CODEC::MakeFourCC(CODEC::FOURCC_HEV1));
    }
    else if (CODEC::Contains(codecs, CODEC::FOURCC_DVHE, codecStr))
    {
      stream.m_info.SetCodecName(CODEC::NAME_HEVC);
      stream.m_info.SetCodecFourCC(CODEC::MakeFourCC(CODEC::FOURCC_DVHE));
    }
    else if (CODEC::Contains(codecs, CODEC::FOURCC_VP09, codecStr) ||
             CODEC::Contains(codecs, CODEC::NAME_VP9, codecStr)) // Some streams incorrectly use the name
    {
      stream.m_info.SetCodecName(CODEC::NAME_VP9);
      if (STRING::Contains(codecStr, "."))
      {
        int codecProfileNum = STRING::ToInt32(codecStr.substr(codecStr.find('.') + 1));
        switch (codecProfileNum)
        {
          case 0:
            stream.m_info.SetCodecProfile(STREAMCODEC_PROFILE::VP9CodecProfile0);
            break;
          case 1:
            stream.m_info.SetCodecProfile(STREAMCODEC_PROFILE::VP9CodecProfile1);
            break;
          case 2:
            stream.m_info.SetCodecProfile(STREAMCODEC_PROFILE::VP9CodecProfile2);
            break;
          case 3:
            stream.m_info.SetCodecProfile(STREAMCODEC_PROFILE::VP9CodecProfile3);
            break;
          default:
            LOG::LogF(LOGWARNING, "Unhandled video codec profile \"%i\" for codec string: %s",
                      codecProfileNum, codecStr.c_str());
            break;
        }
      }
    }
    else if (CODEC::Contains(codecs, CODEC::FOURCC_AV01, codecStr) ||
             CODEC::Contains(codecs, CODEC::NAME_AV1, codecStr)) // Some streams incorrectly use the name
      stream.m_info.SetCodecName(CODEC::NAME_AV1);
    else
    {
      stream.m_isValid = false;
      LOG::LogF(LOGERROR, "Unhandled video codec");
    }
  }
  else if (streamType == StreamType::AUDIO)
  {
    stream.m_info.SetSampleRate(rep->GetSampleRate());
    stream.m_info.SetChannels(rep->GetAudioChannels());

    if (CODEC::Contains(codecs, CODEC::FOURCC_MP4A, codecStr) ||
        CODEC::Contains(codecs, CODEC::FOURCC_AAC_, codecStr))
    {
      stream.m_info.SetCodecName(CODEC::NAME_AAC);

      if (STRING::Contains(codecStr, "mp4a.40.29"))
          stream.m_info.SetCodecProfile(STREAMCODEC_PROFILE::AACCodecProfileHEV2);
      else if (STRING::Contains(codecStr, "mp4a.40.2") || STRING::Contains(codecStr, "mp4a.40.17"))
        stream.m_info.SetCodecProfile(STREAMCODEC_PROFILE::AACCodecProfileLOW); // AAC-LC
      else if (STRING::Contains(codecStr, "mp4a.40.3"))
        stream.m_info.SetCodecProfile(STREAMCODEC_PROFILE::AACCodecProfileSSR);
      else if (STRING::Contains(codecStr, "mp4a.40.4") || STRING::Contains(codecStr, "mp4a.40.19"))
        stream.m_info.SetCodecProfile(STREAMCODEC_PROFILE::AACCodecProfileLTP);
      else if (STRING::Contains(codecStr, "mp4a.40.5"))
        stream.m_info.SetCodecProfile(STREAMCODEC_PROFILE::AACCodecProfileHE);
    }
    else if (CODEC::Contains(codecs, CODEC::FOURCC_DTS_, codecStr))
      stream.m_info.SetCodecName(CODEC::NAME_DTS);
    else if (CODEC::Contains(codecs, CODEC::FOURCC_AC_3, codecStr))
      stream.m_info.SetCodecName(CODEC::NAME_AC3);
    else if (CODEC::Contains(codecs, CODEC::FOURCC_EC_3, codecStr))
    {
      stream.m_info.SetCodecName(CODEC::NAME_EAC3);
      if (CODEC::Contains(codecs, CODEC::NAME_EAC3_JOC))
        stream.m_info.SetCodecProfile(STREAMCODEC_PROFILE::DDPlusCodecProfileAtmos);
    }
    else if (CODEC::Contains(codecs, CODEC::FOURCC_OPUS, codecStr))
      stream.m_info.SetCodecName(CODEC::NAME_OPUS);
    else if (CODEC::Contains(codecs, CODEC::FOURCC_VORB, codecStr) || // Find "vorb" and "vorbis" case
             CODEC::Contains(codecs, CODEC::FOURCC_VORB1, codecStr) ||
             CODEC::Contains(codecs, CODEC::FOURCC_VORB1P, codecStr) ||
             CODEC::Contains(codecs, CODEC::FOURCC_VORB2, codecStr) ||
             CODEC::Contains(codecs, CODEC::FOURCC_VORB2P, codecStr) ||
             CODEC::Contains(codecs, CODEC::FOURCC_VORB3, codecStr) ||
             CODEC::Contains(codecs, CODEC::FOURCC_VORB3P, codecStr))
      stream.m_info.SetCodecName(CODEC::NAME_VORBIS);
    else
    {
      stream.m_isValid = false;
      LOG::LogF(LOGERROR, "Unhandled audio codec");
    }
  }
  else if (streamType == StreamType::SUBTITLE)
  {
    if (CODEC::Contains(codecs, CODEC::FOURCC_TTML, codecStr) ||
        CODEC::Contains(codecs, CODEC::FOURCC_DFXP, codecStr) ||
        CODEC::Contains(codecs, CODEC::FOURCC_STPP, codecStr))
      stream.m_info.SetCodecName(CODEC::NAME_SRT); // We convert it to SRT, Kodi dont support TTML yet
    else if (CODEC::Contains(codecs, CODEC::FOURCC_WVTT, codecStr))
      stream.m_info.SetCodecName(CODEC::NAME_WEBVTT);
    else
    {
      stream.m_isValid = false;
      LOG::LogF(LOGERROR, "Unhandled subtitle codec");
    }
  }

  // Internal codec name can be used by Kodi to detect the codec name to be shown in the GUI track list
  stream.m_info.SetCodecInternalName(codecStr);
}

bool SESSION::CSession::PrepareStream(CStream& stream, uint64_t startPts)
{
  const EVENT_TYPE startEvent = stream.m_adStream.GetStartEvent();
  CPeriod* period = stream.m_adStream.getPeriod();
  CAdaptationSet* adp = stream.m_adStream.getAdaptationSet();
  CRepresentation* repr = stream.m_adStream.getRepresentation();

  // Prepare the representation when the period change usually its not needed,
  // because the timeline is always already updated
  if (m_adaptiveTree->IsReqPrepareStream())
  {
    if ((!m_adaptiveTree->IsChangingPeriod() || repr->Timeline().IsEmpty()) &&
        (startEvent == EVENT_TYPE::STREAM_START || startEvent == EVENT_TYPE::STREAM_ENABLE))
    {
      if (!m_adaptiveTree->PrepareRepresentation(period, adp, repr))
        return false;
    }
  }

  DRM::DRMInfo initDrmInfo;

  if (!repr->DrmInfos().empty())
  {
    DRM::DRMMediaType drmMediaType{DRM::DRMMediaType::UNKNOWN};
    const StreamType sType = adp->GetStreamType();

    if (sType == StreamType::VIDEO || sType == StreamType::VIDEO_AUDIO)
      drmMediaType = DRM::DRMMediaType::VIDEO;
    else if (sType == StreamType::AUDIO)
      drmMediaType = DRM::DRMMediaType::AUDIO;
    else
    {
      LOG::LogF(LOGWARNING, "Stream media type \"%i\" is not supported by the DRM engine",
                static_cast<int>(sType));
      return false;
    }

    if (!m_drmEngine.InitializeSession(repr->DrmInfos(), drmMediaType,
                                       period->IsSecureDecodeNeeded(), stream.m_info, repr, adp,
                                       m_adaptiveTree->IsChangingPeriod(), initDrmInfo))
    {
      return false;
    }
  }

  //! @todo: when the stream will be opened (OpenStream) while you change chapter/period during video seek,
  //!  triggered by DEMUX_SPECIALID_STREAMCHANGE (chapter changed)
  //!  the adaptive stream start to download segments from the start of period, for nothing,
  //!  because when CInputStreamAdaptive::DemuxRead call CSession::OnDemuxRead
  //!  will call Session::SeekTime method that cause to delete and cancel current downloads/buffer,
  //!  and re-start downloads from the appropriate PTS,
  //!  this wastes time for the video seek and resources due to unnecessary downloads
  //!  more likely its more easy to see also cancelled downloads on log with TS streams
  stream.m_adStream.start_stream(startPts);
  stream.SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream.m_adStream));

  ContainerType reprContainerType = repr->GetContainerType();
  uint32_t mask = (1U << stream.m_info.GetStreamType()) | GetIncludedStreamMask();
  auto reader = ADP::CreateStreamReader(reprContainerType, &stream, mask);

  if (!reader)
    return false;

  const auto session = m_drmEngine.GetSession(stream.m_info.GetCryptoSession().GetSessionId(),
                                              initDrmInfo.defaultKid);

  if (adp->GetStreamType() == StreamType::VIDEO || adp->GetStreamType() == StreamType::VIDEO_AUDIO)
  {
    m_reprChooser->SetSecureSession(session && session->capabilities.flags &
                                                   DRM::DecrypterCapabilites::SSD_SECURE_PATH);
  }

  if (session)
    reader->SetDecrypter(session->decrypter, session->capabilities,
                         DRM::ConvertKidStrToBytes(session->kid));

  stream.SetReader(std::move(reader));

  if (reprContainerType == ContainerType::TS || reprContainerType == ContainerType::ADTS)
  {
    // With TS streams the elapsed time would be calculated incorrectly as during the tree refresh,
    // nextSegment would be deleted by the FreeSegments/newsegments swap. Do this now before the tree refresh.
    // Also, when reopening a stream (switching reps) the elapsed time would be incorrectly set until the
    // second segment plays, now force a correct calculation at the start of the stream.
    OnSegmentChanged(&stream.m_adStream);
  }

  return true;
}

std::shared_ptr<CStream> SESSION::CSession::GetStream(unsigned int index) const
{
  return index < m_streams.size() ? m_streams[index] : nullptr;
}

std::shared_ptr<CStream> SESSION::CSession::GetCurrentVideoStream() const
{
  for (auto& stream : m_streams)
  {
    if (stream->m_info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO && stream->IsEnabled())
      return stream;
  }

  return nullptr;
}

void CSession::EnableStream(std::shared_ptr<CStream> stream, bool enable)
{
  if (enable)
  {
    if (!m_timingStream || stream->m_info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
      m_timingStream = stream;

    stream->SetIsEnabled(true);
  }
  else
  {
    if (stream == m_timingStream)
      m_timingStream = nullptr;

    stream->Disable();
  }
}

uint64_t SESSION::CSession::GetTotalTimeMs() const
{
  // In live streaming do not take into account the live delay duration, because its not seekable
  if (m_adaptiveTree->IsLive() && m_adaptiveTree->m_totalTime > m_adaptiveTree->m_liveDelay * 1000)
    return m_adaptiveTree->m_totalTime - m_adaptiveTree->m_liveDelay * 1000;
  else
    return m_adaptiveTree->m_totalTime;
}

uint64_t SESSION::CSession::PTSToElapsed(uint64_t pts)
{
  if (m_timingStream)
  {
    ISampleReader* timingReader{m_timingStream->GetReader()};
    if (!timingReader)
    {
      LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
      return 0;
    }

    // adjusted pts value taking the difference between segment's pts and reader pts
    int64_t manifest_time{static_cast<int64_t>(pts) - timingReader->GetPTSDiff()};
    if (manifest_time < 0)
      manifest_time = 0;

    if (static_cast<uint64_t>(manifest_time) > m_timingStream->m_adStream.GetAbsolutePTSOffset())
      return static_cast<uint64_t>(manifest_time) -
             m_timingStream->m_adStream.GetAbsolutePTSOffset();

    return 0ULL;
  }
  else
    return pts;
}

uint64_t SESSION::CSession::GetTimeshiftBufferStart()
{
  if (m_timingStream)
  {
    ISampleReader* timingReader{m_timingStream->GetReader()};
    if (!timingReader)
    {
      LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
      return 0ULL;
    }
    return m_timingStream->m_adStream.GetAbsolutePTSOffset() + timingReader->GetPTSDiff();
  }
  else
    return 0ULL;
}

void SESSION::CSession::OnScreenResChange()
{
  m_reprChooser->OnUpdateScreenRes();
};

bool SESSION::CSession::GetNextSample(ISampleReader*& sampleReader)
{
  CStream* res{nullptr};
  CStream* waiting{nullptr};

  for (auto& stream : m_streams)
  {
    bool isStarted{false};
    ISampleReader* streamReader{stream->GetReader()};
    if (!streamReader)
      continue;

    if (stream->IsEnabled())
    {
      // Advice is that VP does not want to wait longer than 10ms for a return from
      // DemuxRead() - here we ask to not wait at all and if ReadSample has not yet
      // finished will return "true" to feed an empty packet
      if (streamReader->IsReadSampleAsyncWorking())
      {
        waiting = stream.get();
        break;
      }
      else if (!streamReader->EOS())
      {
        if (AP4_SUCCEEDED(streamReader->Start(isStarted)) && streamReader->IsReady())
        {
          if (!res || streamReader->DTSorPTSManifest() < res->GetReader()->DTSorPTSManifest())
          {
            if (stream->m_adStream.waitingForSegment())
            {
              waiting = stream.get();
            }
            else
            {
              res = stream.get();
            }
          }
        }
      }
    }
  }

  if (waiting)
  {
    return true;
  }
  else if (res)
  {
    ISampleReader* sr{res->GetReader()};

    if (sr->PTS() != STREAM_NOPTS_VALUE)
      m_elapsedTime = PTSToElapsed(sr->PTS()) + GetChapterStartTime();

    sampleReader = sr;
    return true;
  }
  return false;
}

bool SESSION::CSession::SeekTime(double seekTime, bool& isError)
{
  if (m_streams.empty())
    return false;

  //we don't have pts < 0 here and work internally with uint64
  if (seekTime < 0)
    seekTime = 0;

  // Check if we leave our current period
  double chapterTime{0};
  auto pi = m_adaptiveTree->m_periods.cbegin();

  for (; pi != m_adaptiveTree->m_periods.cend(); pi++)
  {
    chapterTime += double((*pi)->GetTlDuration()) / (*pi)->GetTimescale();
    if (chapterTime > seekTime)
      break;
  }

  if (pi == m_adaptiveTree->m_periods.cend())
    --pi;

  chapterTime -= double((*pi)->GetTlDuration()) / (*pi)->GetTimescale();

  if ((*pi).get() != m_adaptiveTree->m_currentPeriod)
  {
    LOG::Log(LOGDEBUG, "SeekTime: seeking into new chapter: %d",
             static_cast<int>((pi - m_adaptiveTree->m_periods.begin()) + 1));
    SeekChapter(static_cast<int>(pi - m_adaptiveTree->m_periods.begin()) + 1);
    m_chapterSeekTime = seekTime;
    return true;
  }

  seekTime -= chapterTime;

  if (m_adaptiveTree->IsLive())
  {
    double delayPtsSecs =
        (static_cast<double>(GetMediaDurationMs()) / 1000) - m_adaptiveTree->m_liveDelay;

    // Check to avoid seek into live delay duration portion, to allow an appropriate live buffering
    if (seekTime > delayPtsSecs)
      seekTime = delayPtsSecs;
  }

  // Helper lambda to check if reader is started,
  // since after seeking across chapters/periods the reader is not started yet
  auto CheckReaderRunning = [this](CStream& stream) -> bool
  {
    auto reader = stream.GetReader();
    if (!reader->IsStarted())
    {
      bool isStarted = false;
      if (AP4_FAILED(reader->Start(isStarted)))
        return false;

      if (isStarted && reader->GetInformation(stream.m_info))
        m_changed = true;
    }
    return true;
  };

  // Helper lambda to perform seek on adaptive stream segment buffer
  auto SeekAdStream = [](CStream& stream, double seekSecs) -> bool
  {
    if (!stream.m_adStream.seek_time(seekSecs))
    {
      stream.GetReader()->Reset(true);
      return false;
    }

    stream.GetReader()->Reset(false);
    return true;
  };

  // correct for starting segment pts value of chapter and chapter offset within program
  uint64_t seekTimeCorrected{static_cast<uint64_t>(seekTime * STREAM_TIME_BASE)};
  int64_t ptsDiff{0};

  // If we have a timing stream, ensure it is started first and compute ptsDiff /
  // corrected seek base that will be used by all other streams.
  if (m_timingStream)
  {
    ISampleReader* timingReader{m_timingStream->GetReader()};
    if (!timingReader)
    {
      LOG::LogF(LOGERROR, "Cannot get the stream sample reader of timing stream");
      return false;
    }

    timingReader->WaitReadSampleAsyncComplete();

    seekTimeCorrected += m_timingStream->m_adStream.GetAbsolutePTSOffset();

    const double seekSecs = static_cast<double>(seekTimeCorrected) / STREAM_TIME_BASE;

    if (!SeekAdStream(*m_timingStream, seekSecs))
    {
      isError = true;
      return false;
    }

    // Note: The following "running" check will download/read the package right now
    // allowing you to read the PTS diff from stream reader
    if (!CheckReaderRunning(*m_timingStream))
      return false;

    ptsDiff = timingReader->GetPTSDiff();

    if (ptsDiff < 0 && seekTimeCorrected + ptsDiff > seekTimeCorrected)
      seekTimeCorrected = 0;
    else
      seekTimeCorrected += ptsDiff;
  }

  // Note: At the end of the seek operations, you may notice on Kodi debug log "dropping packets" prints
  // this happens because we cannot always guarantee a precise seek, for example
  // with FragmentedSampleReader (MP4 samples/packets)
  // we need to start feeding the Kodi demuxer with a synchronization sample/packet
  // but the sync sample (packet) at least for the video track is usually not available for all PTS
  // so we try choose the sample/package closest to the requested "seek" PTS, causing "dropping packets"

  for (auto& stream : m_streams)
  {
    ISampleReader* streamReader{stream->GetReader()};
    if (!streamReader)
      continue;

    streamReader->WaitReadSampleAsyncComplete();
    if (!stream->IsEnabled())
      continue;

    // With FMP4 audio stream included to video stream you must not seek with AdaptiveStream
    // segments are managed by AdaptiveStream of the video stream
    // so CFragmentedSampleReader read data from the reader of the video stream
    const bool hasAdStream = !(streamReader->GetType() == ISampleReader::Type::FMP4 &&
                               stream->m_adStream.getRepresentation()->IsIncludedStream());

    if (hasAdStream && m_timingStream != stream) // Do not "seek time" on "timing stream" already done above
    {
      const double seekSecs{static_cast<double>(seekTimeCorrected - ptsDiff) / STREAM_TIME_BASE};

      if (!SeekAdStream(*stream, seekSecs))
      {
        if (stream->m_info.GetStreamType() == INPUTSTREAM_TYPE_SUBTITLE)
          continue; // Subtitles failure should not block the seek operations

        isError = true;
        return false;
      }
    }

    if (!CheckReaderRunning(*stream))
      return false;

    if (!streamReader->TimeSeek(seekTimeCorrected))
    {
      streamReader->Reset(true);

      if (stream->m_info.GetStreamType() == INPUTSTREAM_TYPE_SUBTITLE)
        continue; // Subtitles failure should not block the seek operations

      isError = true;
      return false;
    }
    else
    {
      double destTime{static_cast<double>(PTSToElapsed(streamReader->PTS())) / STREAM_TIME_BASE};

      // Note: TS sample reader with included streams have a dynamic streamId,
      // so could be that will be printed on log stream id referred to audio or video,
      // maybe this could be improved on sample reader to always start the seek from a video packet
      LOG::Log(LOGINFO, "Seek time %0.1lf for stream: %i continues at %0.1lf (PTS: %llu)", seekTime,
               streamReader->GetStreamId(), destTime, streamReader->PTS());

      // We replace the seek time PTS initially requested with the PTS of the video sample found
      // in order to search the audio/subtitle sample packet more accurately.
      // f.e. in the case of MP4 the video packet has very few sample sync points
      // while the audio stream usually has many sample sync points, this cause a misalignment
      // because for the video you will never find an exact sample for the requested PTS.
      // Then get the nearest PTS found for the video, then align the audio/subtitles with it.
      if (stream->m_info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
      {
        seekTime = destTime;
        seekTimeCorrected = streamReader->PTS();
      }
    }
  }
  return true;
}

void SESSION::CSession::OnDemuxRead()
{
  if (m_adaptiveTree->IsChangingPeriod() && m_adaptiveTree->IsChangingPeriodDone())
  {
    m_adaptiveTree->m_nextPeriod = nullptr;

    if (GetChapterSeekTime() > 0)
    {
      bool isError{false};
      if (!SeekTime(GetChapterSeekTime(), isError))
        DeleteStreams();

      ResetChapterSeekTime();
    }
  }
}

void SESSION::CSession::OnSegmentChanged(adaptive::AdaptiveStream* adStream)
{
  for (auto& stream : m_streams)
  {
    if (&stream->m_adStream == adStream)
    {
      ISampleReader* streamReader{stream->GetReader()};
      if (!streamReader)
        LOG::LogF(LOGWARNING, "Cannot get the stream sample reader");
      else
        streamReader->SetPTSOffset(stream->m_adStream.GetCurrentPTSOffset());

      break;
    }
  }
}

void SESSION::CSession::OnStreamChange(adaptive::AdaptiveStream* adStream)
{
  for (auto& stream : m_streams)
  {
    if (stream->IsEnabled() && &stream->m_adStream == adStream)
    {
      UpdateStream(*stream);
      m_changed = true;
    }
  }
}

bool SESSION::CSession::OnGetStream(int streamid, kodi::addon::InputstreamInfo& info)
{
  if (m_drmEngine.GetStatus() == DRM::EngineStatus::DRM_ERROR ||
      m_drmEngine.GetStatus() == DRM::EngineStatus::DECRYPTER_ERROR)
  {
    // If the stream is protected with a unsupported DRM, we have to stop the playback,
    // since there are no ways to stop playback when Kodi request streams
    // we are forced to delete all CStream's here, so that when demux reader will starts
    // will have no data to process, and so stop the playback
    // (other streams may have been requested/opened before this one)
    DeleteStreams();
    return false;
  }
  else
  {
    auto stream = GetStream(GetStreamIndexFromId(streamid));
    if (!stream)
      return false;

    info = stream->m_info;
  }

  return true;
}

int SESSION::CSession::GetStreamIdFromIndex(int streamIndex) const
{
  // The stream ID is composed as:
  // - the "thousands" part specifies the period/chapter (e.g. 1000 stand for period 1, 2000 for period 2, ...)
  // - the remaining difference from above stand for the stream index

  // Since the index is base 0, we add +1 only to show in the log that stream IDs start from 1
  // (for example 1001 instead of 1000) this is just to maintain consistency with older versions of ISA
  return streamIndex + GetPeriodIndex() * 1000 + 1;
}

unsigned int SESSION::CSession::GetStreamIndexFromId(int streamId) const
{
  return streamId - GetPeriodIndex() * 1000 - 1;
}

uint32_t SESSION::CSession::GetIncludedStreamMask() const
{
  //! @todo: this conversion must be reworked can easily be broken and cause hidden problems
  const INPUTSTREAM_TYPE adp2ips[] = {INPUTSTREAM_TYPE_NONE, INPUTSTREAM_TYPE_VIDEO,
                                      INPUTSTREAM_TYPE_AUDIO, INPUTSTREAM_TYPE_SUBTITLE};
  uint32_t res{0};
  for (unsigned int i(0); i < 4; ++i)
  {
    if (m_adaptiveTree->m_currentPeriod->m_includedStreamType & (1U << i))
      res |= (1U << adp2ips[i]);
  }
  return res;
}

int CSession::GetChapter() const
{
  if (m_adaptiveTree)
  {
    for (auto itPeriod = m_adaptiveTree->m_periods.cbegin();
         itPeriod != m_adaptiveTree->m_periods.cend(); itPeriod++)
    {
      if ((*itPeriod).get() == m_adaptiveTree->m_currentPeriod)
      {
        return static_cast<int>(std::distance(m_adaptiveTree->m_periods.cbegin(), itPeriod)) + 1;
      }
    }
  }
  return 0;
}

int SESSION::CSession::GetChapterCount() const
{
  if (!m_adaptiveTree)
    return 0;

  int count = static_cast<int>(m_adaptiveTree->m_periods.size());
  if (count > 0 && m_adaptiveTree->IsLive())
    count += 1;
  return count;
}

const char* SESSION::CSession::GetChapterName(int number) const
{
  if (!m_adaptiveTree)
    return nullptr;

  --number; // To convert chapter number to index
  if (m_adaptiveTree->IsLive() && number == static_cast<int>(m_adaptiveTree->m_periods.size()))
    return "Live";

  // Chapter name is shown on GUI info window
  // Manifests usually dont provide this info
  // so show the period ID for debugging purpose at request
  if (CSrvBroker::GetSettings().IsDebugVerbose())
  {
    if (number >= 0 && number < static_cast<int>(m_adaptiveTree->m_periods.size()))
      return m_adaptiveTree->m_periods[number]->GetId().c_str();
    return CHAPTER_NAME_UNKNOWN;
  }
  return nullptr;
}

int64_t SESSION::CSession::GetChapterPos(int number) const
{
  if (!m_adaptiveTree)
    return 0;

  --number; // To convert chapter number to index

  if (m_adaptiveTree->IsLive() && number == static_cast<int>(m_adaptiveTree->m_periods.size()))
    return static_cast<int64_t>(GetLiveEdgeMs() / 1000);

  //! @todo: Fragile check, accessing to m_periods can potentially cause problems because
  //! manifest updates can make changes (add/remove) to periods asynchronously.
  //! An appropriate solution must be found, taking into account that these methods
  //! can be called many times during playback. This issue must also be checked in
  //! all other CSession methods on which Kodi core makes callbacks.
  if (number < 0 || number >= static_cast<int>(m_adaptiveTree->m_periods.size()))
    return 0;

  if (number == 0 && m_adaptiveTree->IsLive() && m_adaptiveTree->m_periods.size() == 1 &&
      m_adaptiveTree->m_liveOffset > 0)
    return static_cast<int64_t>(m_adaptiveTree->m_liveOffset);

  int64_t sum{0};

  for (; number; --number)
  {
    sum += (m_adaptiveTree->m_periods[number - 1]->GetTlDuration() * STREAM_TIME_BASE) /
           m_adaptiveTree->m_periods[number - 1]->GetTimescale();
  }

  return sum / STREAM_TIME_BASE;
}

uint64_t SESSION::CSession::GetLiveEdgeMs() const
{
  uint64_t maxTime{0};
  for (const auto& stream : m_streams)
  {
    if (stream->IsEnabled())
    {
      uint64_t curTime = stream->m_adStream.getMaxTimeMs();
      if (curTime > maxTime)
        maxTime = curTime;
    }
  }
  return maxTime > 0 ? maxTime : m_adaptiveTree->m_totalTime;
}

uint64_t SESSION::CSession::GetChapterStartTime() const
{
  uint64_t start_time = 0;
  for (std::unique_ptr<CPeriod>& p : m_adaptiveTree->m_periods)
  {
    if (p.get() == m_adaptiveTree->m_currentPeriod)
      break;
    else
      start_time += (p->GetTlDuration() * STREAM_TIME_BASE) / p->GetTimescale();
  }
  return start_time;
}

int SESSION::CSession::GetPeriodIndex() const
{
  if (!m_adaptiveTree)
    return -1;

  return m_adaptiveTree->m_currentPeriod->GetIndex();
}

bool SESSION::CSession::SeekChapter(int number)
{
  if (!m_adaptiveTree)
    return false;

  if (m_adaptiveTree->IsChangingPeriod())
    return true;

  --number; // To convert chapter number to index

  if (m_adaptiveTree->IsLive() && number == static_cast<int>(m_adaptiveTree->m_periods.size()))
  {
    LOG::LogF(LOGDEBUG, "Seeking to live edge (virtual chapter)");
    SeekTime(static_cast<double>(GetLiveEdgeMs()) / 1000, 0, false);
    return true;
  }

  if (number >= 0 && number < static_cast<int>(m_adaptiveTree->m_periods.size()) &&
      m_adaptiveTree->m_periods[number].get() != m_adaptiveTree->m_currentPeriod)
  {
    CPeriod* nextPeriod = m_adaptiveTree->m_periods[number].get();
    m_adaptiveTree->m_nextPeriod = nextPeriod;
    LOG::LogF(LOGDEBUG, "Switching to new Period (id=%s, start=%llu, seq=%u)",
              nextPeriod->GetId().c_str(), nextPeriod->GetStart(), nextPeriod->GetSequence());

    for (auto& stream : m_streams)
    {
      ISampleReader* sr{stream->GetReader()};
      if (sr)
      {
        sr->WaitReadSampleAsyncComplete();
        sr->Reset(true);
      }
    }
    return true;
  }
  else if (m_adaptiveTree->IsLive() && number >= 0 && number < static_cast<int>(m_adaptiveTree->m_periods.size()))
  {
    double startTime = static_cast<double>(GetChapterPos(number + 1));
    LOG::LogF(LOGDEBUG, "Seeking to start of current Period (startTime=%.3f)", startTime);
    SeekTime(startTime, 0, false);
    return true;
  }
  return false;
}

PLAYLIST::CAdaptationSet* SESSION::CSession::DetermineDefaultAdpSet(PLAYLIST::CPeriod* period)
{
  //! @todo: this is a rough first implementation that have a fixed codec priority order,
  //! and only for video streams. In the future it should be improved
  //! for example check if hardware have capabilities and made it customizable,
  //! since low-end devices may prefer older codecs such as H264 as they may
  //! not have high-performance hardware to process the decoding.
  //! The current sorting is intended just to limit bandwidth consumption
  //! by prioritizing video codecs with high efficiency
  static const std::vector<std::string> videoCodecOrder = {
      CODEC::FOURCC_DVHE, CODEC::FOURCC_HEV1, CODEC::FOURCC_DVH1, CODEC::FOURCC_HVC1,
      CODEC::FOURCC_HEVC, CODEC::FOURCC_AV01, CODEC::NAME_AV1,    CODEC::FOURCC_VP09,
      CODEC::NAME_VP9,    CODEC::FOURCC_AVC_, CODEC::FOURCC_H264};

  CAdaptationSet* defaultAdp{nullptr}; // Default determined by codec order

  for (auto& codecCC : videoCodecOrder)
  {
    for (auto& adp : period->GetAdaptationSets())
    {
      if (adp->GetRepresentations().empty() || adp->GetStreamType() != StreamType::VIDEO)
        continue;

      if (adp->IsDefault()) // Override by manifest custom parameter
      {
        return adp.get();
      }
      else if (CODEC::Contains(adp->GetCodecs(), codecCC) && !defaultAdp)
      {
        defaultAdp = adp.get();
      }
    }
  }

  return defaultAdp;
}

uint64_t SESSION::CSession::GetMediaDurationMs()
{
  if (!m_timingStream || !m_timingStream->IsEnabled())
    return 0;

  return m_timingStream->m_adStream.getMaxTimeMs();
}
