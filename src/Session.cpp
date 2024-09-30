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
#include "decrypters/DrmFactory.h"
#include "decrypters/Helpers.h"
#include "utils/Base64Utils.h"
#include "utils/CurlUtils.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <array>

#include <kodi/addon-instance/Inputstream.h>

using namespace kodi::tools;
using namespace adaptive;
using namespace PLAYLIST;
using namespace SESSION;
using namespace UTILS;

CSession::CSession(const std::string& manifestUrl) : m_manifestUrl(manifestUrl)
{
  m_reprChooser = CHOOSER::CreateRepresentationChooser();

  switch (kodi::addon::GetSettingInt("MEDIATYPE"))
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
}

CSession::~CSession()
{
  LOG::Log(LOGDEBUG, "CSession::~CSession()");
  DeleteStreams();
  DisposeDecrypter();

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
}

void CSession::SetSupportedDecrypterURN(std::vector<std::string_view>& keySystems)
{
  std::string decrypterPath = CSrvBroker::GetSettings().GetDecrypterPath();
  if (decrypterPath.empty())
  {
    LOG::Log(LOGWARNING, "Decrypter path not set in the add-on settings");
    return;
  }

  const std::string keySystem = CSrvBroker::GetKodiProps().GetDrmKeySystem();
  m_decrypter = DRM::FACTORY::GetDecrypter(GetCryptoKeySystem(keySystem));
  if (!m_decrypter)
    return;

  if (!m_decrypter->Initialize())
  {
    LOG::Log(LOGERROR, "The decrypter library cannot be initialized.");
    return;
  }

  keySystems = m_decrypter->SelectKeySystems(keySystem);
  m_decrypter->SetLibraryPath(decrypterPath);
}

void CSession::DisposeSampleDecrypter()
{
  if (m_decrypter)
  {
    for (auto& cdmSession : m_cdmSessions)
    {
      cdmSession.m_cdmSessionStr = nullptr;
      cdmSession.m_cencSingleSampleDecrypter = nullptr;
    }
  }
}

void CSession::DisposeDecrypter()
{
  DisposeSampleDecrypter();
  m_decrypter = nullptr;
}

/*----------------------------------------------------------------------
|   initialize
+---------------------------------------------------------------------*/

bool CSession::Initialize()
{
  auto& kodiProps = CSrvBroker::GetKodiProps();

  // Get URN's wich are supported by this addon
  std::vector<std::string_view> supportedKeySystems;
  if (!kodiProps.GetDrmKeySystem().empty())
  {
    SetSupportedDecrypterURN(supportedKeySystems);
    for (std::string_view keySystem : supportedKeySystems)
    {
      LOG::Log(LOGDEBUG, "Supported URN: %s", keySystem.data());
    }
  }

  std::map<std::string, std::string> manifestHeaders = kodiProps.GetManifestHeaders();
  bool isSessionOpened{false};

  // Preinitialize the DRM, if pre-initialisation data are provided
  if (!kodiProps.GetDrmConfig().preInitData.empty())
  {
    std::string challengeB64;
    std::string sessionId;
    // Pre-initialize the DRM allow to generate the challenge and session ID data
    // used to make licensed manifest requests (via proxy callback)
    if (PreInitializeDRM(challengeB64, sessionId, isSessionOpened))
    {
      manifestHeaders["challengeB64"] = STRING::URLEncode(challengeB64);
      manifestHeaders["sessionId"] = sessionId;
    }
    else
    {
      return false;
    }
  }

  std::string manifestUrl = m_manifestUrl;
  URL::RemovePipePart(manifestUrl); // No pipe char uses, must be used Kodi properties only

  URL::AppendParameters(manifestUrl, kodiProps.GetManifestParams());

  CURL::HTTPResponse manifestResp;
  if (!CURL::DownloadFile(manifestUrl, manifestHeaders, {"etag", "last-modified"}, manifestResp))
    return false;

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
    return false;

  m_adaptiveTree->Configure(m_reprChooser, supportedKeySystems, kodiProps.GetManifestUpdParams());

  if (!m_adaptiveTree->Open(manifestResp.effectiveUrl, manifestResp.headers, manifestResp.data))
  {
    LOG::Log(LOGERROR, "Cannot parse the manifest (%s)", manifestUrl.c_str());
    return false;
  }

  m_adaptiveTree->PostOpen();
  m_reprChooser->PostInit();

  CSrvBroker::GetInstance()->InitStage2(m_adaptiveTree);

  return InitializePeriod(isSessionOpened);
}

void CSession::CheckHDCP()
{
  //! @todo: is needed to implement an appropriate CP check to
  //! remove HDCPOVERRIDE setting workaround
  if (m_cdmSessions.empty())
    return;

  std::vector<DRM::DecrypterCapabilites> decrypterCaps;

  for (const auto& cdmsession : m_cdmSessions)
  {
    decrypterCaps.emplace_back(cdmsession.m_decrypterCaps);
  }

  uint32_t adpIndex{0};
  CAdaptationSet* adp{nullptr};

  while ((adp = m_adaptiveTree->GetAdaptationSet(adpIndex++)))
  {
    if (adp->GetStreamType() != StreamType::VIDEO)
      continue;

    for (auto itRepr = adp->GetRepresentations().begin();
         itRepr != adp->GetRepresentations().end();)
    {
      CRepresentation* repr = (*itRepr).get();

      const DRM::DecrypterCapabilites& ssd_caps = decrypterCaps[repr->m_psshSetPos];

      if (repr->GetHdcpVersion() > ssd_caps.hdcpVersion ||
          (ssd_caps.hdcpLimit > 0 && repr->GetWidth() * repr->GetHeight() > ssd_caps.hdcpLimit))
      {
        LOG::Log(LOGDEBUG, "Representation ID \"%s\" removed as not HDCP compliant",
                 repr->GetId().data());
        itRepr = adp->GetRepresentations().erase(itRepr);
      }
      else
        itRepr++;
    }
  }
}

bool CSession::PreInitializeDRM(std::string& challengeB64,
                                std::string& sessionId,
                                bool& isSessionOpened)
{
  auto& drmPropCfg = CSrvBroker::GetKodiProps().GetDrmConfig();

  std::string psshData;
  std::string kidData;
  // Parse the PSSH/KID data
  size_t posSplitter = drmPropCfg.preInitData.find("|");
  if (posSplitter != std::string::npos)
  {
    psshData = drmPropCfg.preInitData.substr(0, posSplitter);
    kidData = drmPropCfg.preInitData.substr(posSplitter + 1);
  }

  if (psshData.empty() || kidData.empty())
  {
    LOG::LogF(LOGERROR, "Invalid DRM pre-init data, must be as: {PSSH as base64}|{KID as base64}");
    return false;
  }

  m_cdmSessions.resize(2);

  // Try to initialize an SingleSampleDecryptor
  LOG::LogF(LOGDEBUG, "Entering encryption section");

  if (!m_decrypter)
  {
    LOG::LogF(LOGERROR, "No decrypter found for encrypted stream");
    return false;
  }

  if (!m_decrypter->IsInitialised())
  {
    DRM::Config drmCfg = DRM::CreateDRMConfig(DRM::KS_WIDEVINE, drmPropCfg);
    if (!m_decrypter->OpenDRMSystem(drmCfg))
    {
      LOG::LogF(LOGERROR, "OpenDRMSystem failed");
      return false;
    }
  }

  std::vector<uint8_t> initData;

  // Set the provided PSSH
  initData = BASE64::Decode(psshData);

  // Decode the provided KID
  const std::vector<uint8_t> decKid = BASE64::Decode(kidData);

  CCdmSession& session(m_cdmSessions[1]);

  std::string hexKid{STRING::ToHexadecimal(decKid)};
  LOG::LogF(LOGDEBUG, "Initializing session with KID: %s", hexKid.c_str());

  if (m_decrypter && (session.m_cencSingleSampleDecrypter =
                          m_decrypter->CreateSingleSampleDecrypter(initData, decKid, "", true,
                                                                   CryptoMode::AES_CTR)) != nullptr)
  {
    session.m_cdmSessionStr = session.m_cencSingleSampleDecrypter->GetSessionId();
    sessionId = session.m_cdmSessionStr;
    challengeB64 = m_decrypter->GetChallengeB64Data(session.m_cencSingleSampleDecrypter);
  }
  else
  {
    LOG::LogF(LOGERROR, "Initialize failed (SingleSampleDecrypter)");
    session.m_cencSingleSampleDecrypter = nullptr;
    return false;
  }
#if defined(ANDROID)
  // On android is not possible add the default KID key
  // then we cannot re-use same session
  DisposeSampleDecrypter();
#else
  isSessionOpened = true;
#endif
  return true;
}

bool CSession::InitializeDRM(bool addDefaultKID /* = false */)
{
  bool isSecureVideoSession{false};
  m_cdmSessions.resize(m_adaptiveTree->m_currentPeriod->GetPSSHSets().size());

  // Try to initialize an SingleSampleDecryptor
  if (m_adaptiveTree->m_currentPeriod->GetEncryptionState() == EncryptionState::ENCRYPTED_DRM)
  {
    const std::string keySystem = CSrvBroker::GetKodiProps().GetDrmKeySystem();
    auto& drmPropCfg = CSrvBroker::GetKodiProps().GetDrmConfig();

    DRM::Config drmCfg = DRM::CreateDRMConfig(keySystem, drmPropCfg);

    if (drmCfg.license.serverUrl.empty())
      drmCfg.license.serverUrl = m_adaptiveTree->GetLicenseUrl();

    LOG::Log(LOGDEBUG, "Entering encryption section");

    if (!m_decrypter)
    {
      LOG::Log(LOGERROR, "No decrypter found for encrypted stream");
      return false;
    }

    if (!m_decrypter->IsInitialised())
    {
      if (!m_decrypter->OpenDRMSystem(drmCfg))
      {
        LOG::Log(LOGERROR, "OpenDRMSystem failed");
        return false;
      }
    }

    // cdmSession 0 is reserved for unencrypted streams
    for (size_t ses{1}; ses < m_cdmSessions.size(); ++ses)
    {
      CCdmSession& session{m_cdmSessions[ses]};

      // Check if the decrypter has been previously initialized, if so skip it,
      // sessions are collected and never removed and InitializeDRM can be called more times
      // depending on how it is used:
      // 1) CSession::Initialize->InitializePeriod->InitializeDRM - Used by DASH/SS (single call)
      // 2) CInputStreamAdaptive::DemuxRead->m_session->InitializePeriod()->InitializeDRM - On chapter change (single call)
      // 3) CInputStreamAdaptive::OpenStream->m_session->PrepareStream->InitializeDRM - Used by HLS (a call for each stream)
      if (session.m_cencSingleSampleDecrypter)
        continue;

      const CPeriod::PSSHSet& sessionPsshset = m_adaptiveTree->m_currentPeriod->GetPSSHSets()[ses];

      if (sessionPsshset.adaptation_set_->GetStreamType() == StreamType::NOTYPE)
        continue;

      std::vector<uint8_t> initData = sessionPsshset.pssh_;
      std::string defaultKidStr = sessionPsshset.defaultKID_;

      std::vector<uint8_t> customInitData = BASE64::Decode(drmPropCfg.initData);

      if (m_adaptiveTree->GetTreeType() == adaptive::TreeType::SMOOTH_STREAMING &&
          keySystem == DRM::KS_WIDEVINE)
      {
        if (DRM::IsValidPsshHeader(customInitData))
        {
          initData = customInitData;
        }
        else
        {
          LOG::Log(LOGDEBUG, "License data: Create Widevine PSSH for SmoothStreaming %s",
                   customInitData.empty() ? "" : "(with custom data)");

          initData =
              DRM::PSSH::MakeWidevine({DRM::ConvertKidStrToBytes(defaultKidStr)}, customInitData);
        }
      }
      else if (!customInitData.empty())
      {
        // Custom license PSSH data provided from property
        // This can allow to initialize a DRM that could be also not specified
        // as supported in the manifest (e.g. missing DASH ContentProtection tags)
        LOG::Log(LOGDEBUG, "License data: Use PSSH data provided by the license data property");
        initData = customInitData;
      }

      // If no KID, but init data, extract the KID from init data
      if (!initData.empty() && defaultKidStr.empty())
      {
        DRM::PSSH parser;
        if (parser.Parse(initData) && !parser.GetKeyIds().empty())
        {
          LOG::Log(LOGDEBUG, "Default KID parsed from init data");
          defaultKidStr = STRING::ToHexadecimal(parser.GetKeyIds()[0]);
        }
      }

      //! @todo: as is implemented InitializeDRM will initialize all PSSHSet's also when are not used,
      //!   therefore ExtractStreamProtectionData can perform many (not needed) downloads of mp4 init files
      if ((initData.empty() && keySystem != DRM::KS_CLEARKEY) || defaultKidStr.empty())
      {
        // Try extract the PSSH/KID from the stream
        ExtractStreamProtectionData(sessionPsshset, defaultKidStr, initData,
                                    m_adaptiveTree->m_supportedKeySystems);
      }

      const std::vector<uint8_t> defaultKid = DRM::ConvertKidStrToBytes(defaultKidStr);

      if (addDefaultKID && ses == 1 && session.m_cencSingleSampleDecrypter)
      {
        // If the CDM has been pre-initialized, on non-android systems
        // we use the same session opened then we have to add the current KID
        // because the session has been opened with a different PSSH/KID
        session.m_cencSingleSampleDecrypter->AddKeyId(defaultKid);
        session.m_cencSingleSampleDecrypter->SetDefaultKeyId(defaultKid);
      }

      if (!defaultKid.empty())
      {
        LOG::Log(LOGDEBUG, "Initializing stream with KID: %s", defaultKidStr.c_str());

        // If a decrypter has the default KID, re-use the same decrypter for also this session
        for (size_t i{1}; i < ses; ++i)
        {
          if (m_decrypter->HasLicenseKey(m_cdmSessions[i].m_cencSingleSampleDecrypter, defaultKid))
          {
            session.m_cencSingleSampleDecrypter = m_cdmSessions[i].m_cencSingleSampleDecrypter;
            break;
          }
        }
      }
      else if (defaultKid.empty())
      {
        for (size_t i{1}; i < ses; ++i)
        {
          if (sessionPsshset.pssh_ == m_adaptiveTree->m_currentPeriod->GetPSSHSets()[i].pssh_)
          {
            session.m_cencSingleSampleDecrypter = m_cdmSessions[i].m_cencSingleSampleDecrypter;
            break;
          }
        }
        if (!session.m_cencSingleSampleDecrypter)
        {
          LOG::Log(LOGWARNING, "Initializing stream with unknown KID!");
        }
      }

      if (session.m_cencSingleSampleDecrypter ||
          (session.m_cencSingleSampleDecrypter = m_decrypter->CreateSingleSampleDecrypter(
               initData, defaultKid, sessionPsshset.m_licenseUrl, false,
               sessionPsshset.m_cryptoMode == CryptoMode::NONE ? CryptoMode::AES_CTR
                                                               : sessionPsshset.m_cryptoMode)) !=
              nullptr)
      {
        m_decrypter->GetCapabilities(session.m_cencSingleSampleDecrypter, defaultKid,
                                     sessionPsshset.media_, session.m_decrypterCaps);

        session.m_cdmSessionStr = session.m_cencSingleSampleDecrypter->GetSessionId();

        if (session.m_decrypterCaps.flags & DRM::DecrypterCapabilites::SSD_INVALID)
        {
          m_adaptiveTree->m_currentPeriod->RemovePSSHSet(static_cast<std::uint16_t>(ses));
        }
        else if (session.m_decrypterCaps.flags & DRM::DecrypterCapabilites::SSD_SECURE_PATH)
        {
          isSecureVideoSession = true;

          // Allow to disable the secure decoder
          bool disableSecureDecoder = CSrvBroker::GetSettings().IsDisableSecureDecoder();
          // but, DRM config can override it
          if (drmPropCfg.isSecureDecoderEnabled.has_value())
            disableSecureDecoder = !*drmPropCfg.isSecureDecoderEnabled;
          // but, manifest config can override all others
          if (m_adaptiveTree->m_currentPeriod->IsSecureDecodeNeeded().has_value())
            disableSecureDecoder = !*m_adaptiveTree->m_currentPeriod->IsSecureDecodeNeeded();
          if (disableSecureDecoder)
          {
            LOG::Log(LOGDEBUG, "Initialize DRM: Configured with secure decoder disabled");
            session.m_decrypterCaps.flags &= ~DRM::DecrypterCapabilites::SSD_SECURE_DECODER;
          }
        }
      }
      else
      {
        LOG::Log(LOGERROR, "Initialize failed (SingleSampleDecrypter)");
        for (size_t i(ses); i < m_cdmSessions.size(); ++i)
          m_cdmSessions[i].m_cencSingleSampleDecrypter = nullptr;

        return false;
      }
    }
  }

  bool isHdcpOverride = CSrvBroker::GetSettings().IsHdcpOverride();
  if (isHdcpOverride)
    LOG::Log(LOGDEBUG, "Ignore HDCP status is enabled");

  if (!isHdcpOverride)
    CheckHDCP();

  m_reprChooser->SetSecureSession(isSecureVideoSession);

  return true;
}

bool CSession::InitializePeriod(bool isSessionOpened /* = false */)
{
  bool isPsshChanged{true};
  bool isReusePssh{true};

  if (m_adaptiveTree->IsChangingPeriod())
  {
    isPsshChanged =
        !(m_adaptiveTree->m_currentPeriod->GetPSSHSets() == m_adaptiveTree->m_nextPeriod->GetPSSHSets());
    isReusePssh = !isPsshChanged && m_adaptiveTree->m_nextPeriod->GetEncryptionState() ==
                                       EncryptionState::ENCRYPTED_DRM;
    m_adaptiveTree->m_currentPeriod = m_adaptiveTree->m_nextPeriod;
  }

  m_chapterStartTime = GetChapterStartTime();

  if (m_adaptiveTree->m_currentPeriod->GetEncryptionState() == EncryptionState::NOT_SUPPORTED)
  {
    LOG::LogF(LOGERROR, "Unhandled encrypted stream.");
    return false;
  }

  // create SESSION::STREAM objects. One for each AdaptationSet
  m_streams.clear();

  if (!isPsshChanged)
  {
    if (isReusePssh)
      LOG::Log(LOGDEBUG, "Reusing DRM psshSets for new period!");
  }
  else
  {
    if (isSessionOpened)
    {
      LOG::Log(LOGDEBUG, "New period, reinitialize by using same session");
    }
    else
    {
      LOG::Log(LOGDEBUG, "New period, dispose sample decrypter and reinitialize");
      DisposeSampleDecrypter();
    }

    if (!InitializeDRM(isSessionOpened))
      return false;
  }

  uint32_t adpIndex{0};
  CAdaptationSet* adp{nullptr};
  CHOOSER::StreamSelection streamSelectionMode = m_reprChooser->GetStreamSelectionMode();
  //! @todo: GetAudioLangOrig property should be reworked to allow override or set
  //! manifest a/v and subtitles streams attributes such as default/original etc..
  //! since Kodi stream flags dont have always the same meaning of manifest attributes
  //! and some video services dont follow exactly the specs so can lead to wrong Kodi flags sets.
  //! An idea is add/move these override of attributes on post manifest parsing.
  std::string audioLanguageOrig = CSrvBroker::GetKodiProps().GetAudioLangOrig();

  while ((adp = m_adaptiveTree->GetAdaptationSet(adpIndex++)))
  {
    if (adp->GetRepresentations().empty())
      continue;

    if (adp->GetStreamType() == StreamType::NOTYPE)
    {
      LOG::LogF(LOGDEBUG, "Skipped streams on adaptation set id \"%s\" due to unsupported/unknown type",
                adp->GetId().data());
      continue;
    }

    bool isManualStreamSelection;
    if (adp->GetStreamType() == StreamType::VIDEO)
      isManualStreamSelection = streamSelectionMode != CHOOSER::StreamSelection::AUTO;
    else
      isManualStreamSelection = streamSelectionMode == CHOOSER::StreamSelection::MANUAL;

    // Get the default initial stream repr. based on "adaptive repr. chooser"
    auto defaultRepr{m_reprChooser->GetRepresentation(adp)};

    if (isManualStreamSelection)
    {
      // Add all stream representations
      for (size_t i{0}; i < adp->GetRepresentations().size(); i++)
      {
        size_t reprIndex{adp->GetRepresentations().size() - i};
        uint32_t uniqueId{adpIndex};
        uniqueId |= reprIndex << 16;

        CRepresentation* currentRepr = adp->GetRepresentations()[i].get();
        bool isDefaultRepr{currentRepr == defaultRepr};

        AddStream(adp, currentRepr, isDefaultRepr, uniqueId, audioLanguageOrig);
      }
    }
    else
    {
      // Add the default stream representation only
      size_t reprIndex{adp->GetRepresentations().size()};
      uint32_t uniqueId{adpIndex};
      uniqueId |= reprIndex << 16;

      AddStream(adp, defaultRepr, true, uniqueId, audioLanguageOrig);
    }
  }

  return true;
}

void CSession::AddStream(PLAYLIST::CAdaptationSet* adp,
                         PLAYLIST::CRepresentation* initialRepr,
                         bool isDefaultRepr,
                         uint32_t uniqueId,
                         std::string_view audioLanguageOrig)
{
  m_streams.push_back(std::make_unique<CStream>(m_adaptiveTree, adp, initialRepr));

  CStream& stream{*m_streams.back()};

  uint32_t flags{INPUTSTREAM_FLAG_NONE};
  stream.m_info.SetName(adp->GetName());

  switch (adp->GetStreamType())
  {
    case StreamType::VIDEO:
    {
      stream.m_info.SetStreamType(INPUTSTREAM_TYPE_VIDEO);
      if (isDefaultRepr)
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
  stream.m_info.SetLanguage(adp->GetLanguage());
  stream.m_info.ClearExtraData();
  stream.m_info.SetFeatures(0);

  stream.m_adStream.set_observer(dynamic_cast<adaptive::AdaptiveStreamObserver*>(this));

  UpdateStream(stream);
}

void CSession::UpdateStream(CStream& stream)
{
  // On this method we set stream info provided by manifest parsing, but these info could be
  // changed by sample readers just before the start of playback by using GetInformation() methods
  const StreamType streamType = stream.m_adStream.getAdaptationSet()->GetStreamType();
  CRepresentation* rep{stream.m_adStream.getRepresentation()};

  if (rep->GetContainerType() == ContainerType::INVALID)
  {
    LOG::LogF(LOGERROR, "Container type not valid on stream representation ID: %s",
              rep->GetId().data());
    stream.m_isValid = false;
    return;
  }

  stream.m_isEncrypted = rep->GetPsshSetPos() != PSSHSET_POS_DEFAULT;
  stream.m_info.SetExtraData(nullptr, 0);

  if (!rep->GetCodecPrivateData().empty())
  {
    std::vector<uint8_t> annexb;
    const std::vector<uint8_t>* extraData(&annexb);

    const DRM::DecrypterCapabilites& caps{GetDecrypterCaps(rep->m_psshSetPos)};

    if ((caps.flags & DRM::DecrypterCapabilites::SSD_ANNEXB_REQUIRED) &&
        stream.m_info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
    {
      LOG::Log(LOGDEBUG, "UpdateStream: Convert avc -> annexb");
      annexb = AvcToAnnexb(rep->GetCodecPrivateData());
    }
    else
    {
      extraData = &rep->GetCodecPrivateData();
    }
    stream.m_info.SetExtraData(extraData->data(), extraData->size());
  }

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
      stream.m_info.SetCodecName(CODEC::NAME_AAC);
    else if (CODEC::Contains(codecs, CODEC::FOURCC_DTS_, codecStr))
      stream.m_info.SetCodecName(CODEC::NAME_DTS);
    else if (CODEC::Contains(codecs, CODEC::FOURCC_AC_3, codecStr))
      stream.m_info.SetCodecName(CODEC::NAME_AC3);
    else if (CODEC::Contains(codecs, CODEC::NAME_EAC3_JOC, codecStr) ||
             CODEC::Contains(codecs, CODEC::FOURCC_EC_3, codecStr))
    {
      // In the above condition above is checked NAME_EAC3_JOC as first,
      // in order to get the codec string to signal DD+ Atmos in to the SetCodecInternalName
      stream.m_info.SetCodecName(CODEC::NAME_EAC3);
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

void CSession::PrepareStream(CStream* stream)
{
  if (!m_adaptiveTree->IsReqPrepareStream())
    return;

  CRepresentation* repr = stream->m_adStream.getRepresentation();
  const EVENT_TYPE startEvent = stream->m_adStream.GetStartEvent();

  // Prepare the representation when the period change usually its not needed,
  // because the timeline is always already updated
  if ((!m_adaptiveTree->IsChangingPeriod() || repr->Timeline().IsEmpty()) &&
      (startEvent == EVENT_TYPE::STREAM_START || startEvent == EVENT_TYPE::STREAM_ENABLE))
  {
    m_adaptiveTree->PrepareRepresentation(stream->m_adStream.getPeriod(),
                                          stream->m_adStream.getAdaptationSet(), repr);
  }

  if (stream->m_adStream.getPeriod()->GetEncryptionState() == EncryptionState::ENCRYPTED_DRM)
  {
    InitializeDRM();
  }

  stream->m_isEncrypted = repr->GetPsshSetPos() != PSSHSET_POS_DEFAULT;
}

void CSession::EnableStream(CStream* stream, bool enable)
{
  if (enable)
  {
    if (!m_timingStream)
      m_timingStream = stream;

    stream->m_isEnabled = true;
  }
  else
  {
    if (stream == m_timingStream)
      m_timingStream = nullptr;

    stream->Disable();
  }
}

bool SESSION::CSession::IsCDMSessionSecurePath(size_t index)
{
  if (index >= m_cdmSessions.size())
  {
    LOG::LogF(LOGERROR, "No CDM session at index %u", index);
    return false;
  }

  return (m_cdmSessions[index].m_decrypterCaps.flags &
          DRM::DecrypterCapabilites::SSD_SECURE_PATH) != 0;
}

const char* SESSION::CSession::GetCDMSession(unsigned int index)
{
  if (index >= m_cdmSessions.size())
  {
    LOG::LogF(LOGERROR, "No CDM session at index %u", index);
    return nullptr;
  }
  return m_cdmSessions[index].m_cdmSessionStr;
}

uint64_t CSession::PTSToElapsed(uint64_t pts)
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

uint64_t CSession::GetTimeshiftBufferStart()
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

// TODO: clean this up along with seektime
void CSession::StartReader(
    CStream* stream, uint64_t seekTime, int64_t ptsDiff, bool preceeding, bool timing)
{
  ISampleReader* streamReader = stream->GetReader();
  if (!streamReader)
  {
    LOG::LogF(LOGERROR, "Cannot get the stream reader");
    return;
  }

  bool bReset = true;
  if (timing)
    seekTime += stream->m_adStream.GetAbsolutePTSOffset();
  else
    seekTime -= ptsDiff;

  stream->m_adStream.seek_time(static_cast<double>(seekTime / STREAM_TIME_BASE), preceeding,
                               bReset);

  if (bReset)
    streamReader->Reset(false);

  bool bStarted = false;
  streamReader->Start(bStarted);
  if (bStarted && (streamReader->GetInformation(stream->m_info)))
    m_changed = true;
}

void CSession::SetVideoResolution(int width, int height, int maxWidth, int maxHeight)
{
  m_reprChooser->SetScreenResolution(width, height, maxWidth, maxHeight);
};

bool CSession::GetNextSample(ISampleReader*& sampleReader)
{
  CStream* res{nullptr};
  CStream* waiting{nullptr};

  for (auto& stream : m_streams)
  {
    bool isStarted{false};
    ISampleReader* streamReader{stream->GetReader()};
    if (!streamReader)
      continue;

    if (stream->m_isEnabled)
    {
      // Advice is that VP does not want to wait longer than 10ms for a return from
      // DemuxRead() - here we ask to not wait at all and if ReadSample has not yet
      // finished we return the dummy reader instead
      if (streamReader->IsReadSampleAsyncWorking())
      {
        waiting = stream.get();
        break;
      }
      else if (streamReader->IsReady() && !streamReader->EOS())
      {
        if (AP4_SUCCEEDED(streamReader->Start(isStarted)))
        {
          //!@ todo: DTSorPTS comparison is wrong
          //! currently we are compare audio/video/subtitles
          //! for audio/video the pts/dts come from demuxer, but subtitles use pts from manifest
          //! these values not always are comparable because pts/dts that come from demuxer packet data
          //! can be different and makes this package selection ineffective
          //! see also workaround on CSubtitleSampleReader::ReadSample
          if (!res || streamReader->DTSorPTS() < res->GetReader()->DTSorPTS())
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

bool CSession::SeekTime(double seekTime, unsigned int streamId, bool preceeding)
{
  bool ret{false};

  //we don't have pts < 0 here and work internally with uint64
  if (seekTime < 0)
    seekTime = 0;

  // Check if we leave our current period
  double chapterTime{0};
  auto pi = m_adaptiveTree->m_periods.cbegin();

  for (; pi != m_adaptiveTree->m_periods.cend(); pi++)
  {
    chapterTime += double((*pi)->GetDuration()) / (*pi)->GetTimescale();
    if (chapterTime > seekTime)
      break;
  }

  if (pi == m_adaptiveTree->m_periods.cend())
    --pi;

  chapterTime -= double((*pi)->GetDuration()) / (*pi)->GetTimescale();

  if ((*pi).get() != m_adaptiveTree->m_currentPeriod)
  {
    LOG::Log(LOGDEBUG, "SeekTime: seeking into new chapter: %d",
             static_cast<int>((pi - m_adaptiveTree->m_periods.begin()) + 1));
    SeekChapter(static_cast<int>(pi - m_adaptiveTree->m_periods.begin()) + 1);
    m_chapterSeekTime = seekTime;
    return true;
  }

  seekTime -= chapterTime;

  // don't try to seek past the end of the stream, leave a sensible amount so we can buffer properly
  if (m_adaptiveTree->IsLive())
  {
    double maxSeek{0};
    uint64_t curTime;
    uint64_t maxTime{0};
    for (auto& stream : m_streams)
    {
      if (stream->m_isEnabled && (curTime = stream->m_adStream.getMaxTimeMs()) && curTime > maxTime)
      {
        maxTime = curTime;
      }
    }

    maxSeek = (static_cast<double>(maxTime) / 1000) - m_adaptiveTree->m_liveDelay;
    if (maxSeek < 0)
      maxSeek = 0;

    if (seekTime > maxSeek)
      seekTime = maxSeek;
  }

  // correct for starting segment pts value of chapter and chapter offset within program
  uint64_t seekTimeCorrected{static_cast<uint64_t>(seekTime * STREAM_TIME_BASE)};
  int64_t ptsDiff{0};
  if (m_timingStream)
  {
    // after seeking across chapters with fmp4 streams the reader will not have started
    // so we start here to ensure that we have the required information to correctly
    // seek with proper stream alignment
    ISampleReader* timingReader{m_timingStream->GetReader()};
    if (!timingReader)
    {
      LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
      return false;
    }
    timingReader->WaitReadSampleAsyncComplete();
    if (!timingReader->IsStarted())
      StartReader(m_timingStream, seekTimeCorrected, ptsDiff, preceeding, true);

    seekTimeCorrected += m_timingStream->m_adStream.GetAbsolutePTSOffset();
    ptsDiff = timingReader->GetPTSDiff();
    if (ptsDiff < 0 && seekTimeCorrected + ptsDiff > seekTimeCorrected)
      seekTimeCorrected = 0;
    else
      seekTimeCorrected += ptsDiff;
  }

  for (auto& stream : m_streams)
  {
    ISampleReader* streamReader{stream->GetReader()};
    if (!streamReader)
      continue;

    streamReader->WaitReadSampleAsyncComplete();
    if (stream->m_isEnabled && (streamId == 0 || stream->m_info.GetPhysicalIndex() == streamId))
    {
      bool reset{true};
      // all streams must be started before seeking to ensure cross chapter seeks
      // will seek to the correct location/segment
      if (!streamReader->IsStarted())
        StartReader(stream.get(), seekTimeCorrected, ptsDiff, preceeding, false);

      streamReader->SetPTSDiff(ptsDiff);
      
      double seekSecs{static_cast<double>(seekTimeCorrected - streamReader->GetPTSDiff()) /
                      STREAM_TIME_BASE};
      if (stream->m_adStream.seek_time(seekSecs, preceeding, reset))
      {
        if (reset)
          streamReader->Reset(false);
        // advance reader to requested time
        if (!streamReader->TimeSeek(seekTimeCorrected, preceeding))
        {
          streamReader->Reset(true);
        }
        else
        {
          double destTime{static_cast<double>(PTSToElapsed(streamReader->PTS())) /
                          STREAM_TIME_BASE};
          LOG::Log(LOGINFO,
                   "Seek time %0.1lf for stream: %u (physical index %u) continues at %0.1lf "
                   "(PTS: %llu)",
                   seekTime, streamReader->GetStreamId(), stream->m_info.GetPhysicalIndex(),
                   destTime, streamReader->PTS());
          if (stream->m_info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
          {
            seekTime = destTime;
            seekTimeCorrected = streamReader->PTS();
            preceeding = false;
          }
          ret = true;
        }
      }
      else
        streamReader->Reset(true);
    }
  }

  return ret;
}

void CSession::OnDemuxRead()
{
  if (m_adaptiveTree->IsChangingPeriod() && m_adaptiveTree->IsChangingPeriodDone())
  {
    m_adaptiveTree->m_nextPeriod = nullptr;

    if (GetChapterSeekTime() > 0)
    {
      SeekTime(GetChapterSeekTime());
      ResetChapterSeekTime();
    }
  }
}

void CSession::OnSegmentChanged(adaptive::AdaptiveStream* adStream)
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

void CSession::OnStreamChange(adaptive::AdaptiveStream* adStream)
{
  for (auto& stream : m_streams)
  {
    if (stream->m_isEnabled && &stream->m_adStream == adStream)
    {
      UpdateStream(*stream);
      m_changed = true;
    }
  }
}

std::shared_ptr<Adaptive_CencSingleSampleDecrypter> CSession::GetSingleSampleDecrypter(std::string sessionId)
{
  for (std::vector<CCdmSession>::iterator b(m_cdmSessions.begin() + 1), e(m_cdmSessions.end());
       b != e; ++b)
  {
    if (b->m_cdmSessionStr && sessionId == b->m_cdmSessionStr)
      return b->m_cencSingleSampleDecrypter;
  }
  return nullptr;
}

uint32_t CSession::GetIncludedStreamMask() const
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

STREAM_CRYPTO_KEY_SYSTEM CSession::GetCryptoKeySystem(std::string_view keySystem) const
{
  if (keySystem == DRM::KS_WIDEVINE)
    return STREAM_CRYPTO_KEY_SYSTEM_WIDEVINE;
  else if (keySystem == DRM::KS_WISEPLAY)
    return STREAM_CRYPTO_KEY_SYSTEM_WISEPLAY;
  else if (keySystem == DRM::KS_PLAYREADY)
    return STREAM_CRYPTO_KEY_SYSTEM_PLAYREADY;
  else if (keySystem == DRM::KS_CLEARKEY)
    return STREAM_CRYPTO_KEY_SYSTEM_CLEARKEY;
  else
    return STREAM_CRYPTO_KEY_SYSTEM_NONE;
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
  return -1;
}

int CSession::GetChapterCount() const
{
  if (m_adaptiveTree && m_adaptiveTree->m_periods.size() > 1)
      return static_cast<int>(m_adaptiveTree->m_periods.size());

  return 0;
}

std::string CSession::GetChapterName(int ch) const
{
  if (m_adaptiveTree)
  {
    --ch;
    if (ch >= 0 && ch < static_cast<int>(m_adaptiveTree->m_periods.size()))
      return m_adaptiveTree->m_periods[ch]->GetId().data();
  }

  return "[Unknown]";
}

int64_t CSession::GetChapterPos(int ch) const
{
  int64_t sum{0};
  --ch;

  for (; ch; --ch)
  {
    sum += (m_adaptiveTree->m_periods[ch - 1]->GetDuration() * STREAM_TIME_BASE) /
           m_adaptiveTree->m_periods[ch - 1]->GetTimescale();
  }

  return sum / STREAM_TIME_BASE;
}

uint64_t CSession::GetChapterStartTime() const
{
  uint64_t start_time = 0;
  for (std::unique_ptr<CPeriod>& p : m_adaptiveTree->m_periods)
  {
    if (p.get() == m_adaptiveTree->m_currentPeriod)
      break;
    else
      start_time += (p->GetDuration() * STREAM_TIME_BASE) / p->GetTimescale();
  }
  return start_time;
}

int CSession::GetPeriodId() const
{
  if (m_adaptiveTree)
  {
    if (IsLive())
    {
      if (m_adaptiveTree->initial_sequence_.has_value() &&
          m_adaptiveTree->m_currentPeriod->GetSequence() == *m_adaptiveTree->initial_sequence_)
      {
        return 1;
      }
      return m_adaptiveTree->m_currentPeriod->GetSequence() + 1;
    }
    else
      return GetChapter();
  }
  return -1;
}

bool CSession::SeekChapter(int ch)
{
  if (m_adaptiveTree->IsChangingPeriod())
    return true;

  --ch;
  if (ch >= 0 && ch < static_cast<int>(m_adaptiveTree->m_periods.size()) &&
      m_adaptiveTree->m_periods[ch].get() != m_adaptiveTree->m_currentPeriod)
  {
    CPeriod* nextPeriod = m_adaptiveTree->m_periods[ch].get();
    m_adaptiveTree->m_nextPeriod = nextPeriod;
    LOG::LogF(LOGDEBUG, "Switching to new Period (id=%s, start=%llu, seq=%u)",
              nextPeriod->GetId().data(), nextPeriod->GetStart(), nextPeriod->GetSequence());

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
  return false;
}

void CSession::ExtractStreamProtectionData(const PLAYLIST::CPeriod::PSSHSet& psshSet,
                                           std::string& defaultKid,
                                           std::vector<uint8_t>& initData,
                                           const std::vector<std::string_view>& keySystems)
{
  auto initialRepr = m_reprChooser->GetRepresentation(psshSet.adaptation_set_);

  if (initialRepr->GetContainerType() != ContainerType::MP4)
    return;

  LOG::LogF(LOGDEBUG, "Parse protection data from stream");
  CStream stream{m_adaptiveTree, psshSet.adaptation_set_, initialRepr};

  stream.m_isEnabled = true;
  stream.m_adStream.start_stream();
  stream.SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream.m_adStream));
  stream.SetStreamFile(std::make_unique<AP4_File>(*stream.GetAdByteStream(),
                                                  AP4_DefaultAtomFactory::Instance_, true));
  AP4_Movie* movie{stream.GetStreamFile()->GetMovie()};
  if (!movie)
  {
    LOG::LogF(LOGERROR, "No MOOV atom in stream");
    stream.Disable();
    return;
  }

  AP4_Track* track =
      movie->GetTrack(static_cast<AP4_Track::Type>(stream.m_adStream.GetTrackType()));

  if (track) // Try extract the default KID from tenc / piff mp4 box
  {
    AP4_ProtectedSampleDescription* protSampleDesc =
        static_cast<AP4_ProtectedSampleDescription*>(track->GetSampleDescription(0));

    if (protSampleDesc)
    {
      AP4_ProtectionSchemeInfo* psi = protSampleDesc->GetSchemeInfo();
      if (psi)
      {
        AP4_ContainerAtom* schi = protSampleDesc->GetSchemeInfo()->GetSchiAtom();
        if (schi)
        {
          AP4_TencAtom* tenc =
              AP4_DYNAMIC_CAST(AP4_TencAtom, schi->GetChild(AP4_ATOM_TYPE_TENC, 0));
          if (tenc)
          {
            defaultKid = STRING::ToHexadecimal(tenc->GetDefaultKid(), 16);
          }
          else
          {
            AP4_PiffTrackEncryptionAtom* piff =
                AP4_DYNAMIC_CAST(AP4_PiffTrackEncryptionAtom,
                                 schi->GetChild(AP4_UUID_PIFF_TRACK_ENCRYPTION_ATOM, 0));
            if (piff)
            {
              defaultKid = STRING::ToHexadecimal(piff->GetDefaultKid(), 16);
            }
          }
        }
      }
    }
  }

  if (initData.empty() || defaultKid.empty())
  {
    const std::vector<std::string> systemIds = DRM::UrnsToSystemIds(keySystems);
    AP4_Array<AP4_PsshAtom>& pssh{movie->GetPsshAtoms()};

    for (unsigned int i = 0; i < pssh.ItemCount(); ++i)
    {
      AP4_PsshAtom& psshAtom = pssh[i];

      std::string systemId = STRING::ToHexadecimal(psshAtom.GetSystemId(), 16);

      // Check if the system id is supported
      if (std::find(systemIds.cbegin(), systemIds.cend(), systemId) != systemIds.cend())
      {
        const AP4_DataBuffer& dataBuf = psshAtom.GetData();
        const std::vector<uint8_t> psshData{dataBuf.GetData(),
                                            dataBuf.GetData() + dataBuf.GetDataSize()};

        initData = DRM::PSSH::Make(psshAtom.GetSystemId(), {}, psshData);

        if (psshAtom.GetKid(0))
        {
          defaultKid = STRING::ToHexadecimal(pssh[i].GetKid(0), 16);
        }

        break;
      }
    }
  }

  stream.Disable();
}
