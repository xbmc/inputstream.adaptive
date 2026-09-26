/*
 *  Copyright (C) 2025 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DrmEngine.h"

#include "CompKodiProps.h"
#include "CompResources.h"
#include "CompSettings.h"
#include "DrmFactory.h"
#include "Helpers.h"
#include "SrvBroker.h"
#include "common/AdaptiveDecrypter.h"
#include "utils/Base64Utils.h"
#include "utils/GUIUtils.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/log.h"

#include <nlohmann/json.hpp>

using njson = nlohmann::json;
using namespace DRM;
using namespace UTILS;

namespace
{
STREAM_CRYPTO_KEY_SYSTEM KSToCryptoKeySystem(std::string_view keySystem)
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

/*!
 * \brief Get a DRMInfo by Key System
 * \param drmInfos The manifest DRM info
 * \param keySystem The Key System to search
 * \param isStrict If true only match the provided key system, otherwise also match empty key system (CENC)
 * \return The DRMInfo if found, otherwise nullptr
 */
DRM::DRMInfo* GetDRMInfoByKS(std::vector<DRM::DRMInfo>& drmInfos, std::string_view keySystem, bool isStrict = false)
{
  // Give priority to entries that explicitly match the requested key system, then try find entries with an empty key system (CENC)
  auto itDrmInfo = std::find_if(drmInfos.begin(), drmInfos.end(), [&](const DRMInfo& info)
                                { return !info.keySystem.empty() && info.keySystem == keySystem; });

  if (itDrmInfo != drmInfos.end())
    return &(*itDrmInfo);

  if (!isStrict)
  {
    itDrmInfo = std::find_if(drmInfos.begin(), drmInfos.end(), [&](const DRMInfo& info)
                             { return info.keySystem.empty(); });

    if (itDrmInfo != drmInfos.end())
      return &(*itDrmInfo);
  }

  return nullptr;
}

std::vector<DRM::DRMInfo> GetDRMInfosByKS(std::vector<DRM::DRMInfo>& drmInfos, std::string_view keySystem)
{
  std::vector<DRM::DRMInfo> ret;
  // Give priority to entries that explicitly match the requested key system, then append entries with an empty key system (CENC)
  for (const auto& info : drmInfos)
  {
    if (!info.keySystem.empty() && info.keySystem == keySystem)
      ret.emplace_back(info);
  }
  for (const auto& info : drmInfos)
  {
    if (info.keySystem.empty())
      ret.emplace_back(info);
  }

  return ret;
}

// \brief Common CENC DRMInfo need to be converted to a specific key system with appropriate PSSH init
void ConvertDRMInfoCENC(DRM::DRMInfo& drmInfo, const std::string& keySystem)
{
  if (!drmInfo.keySystem.empty())
    return; // Not a CENC DRMInfo, no need to convert

  LOG::Log(LOGDEBUG, "Converting Common CENC DRMInfo to %s", keySystem.c_str());
  std::vector<std::vector<uint8_t>> keyIds;

  if (DRM::IsValidPsshHeader(drmInfo.initData))
  {
    DRM::PSSH parser;
    if (parser.Parse(drmInfo.initData))
      keyIds = parser.GetKeyIds();
  }

  if (keyIds.empty())
  {
    if (drmInfo.defaultKid.empty())
    {
      LOG::Log(LOGERROR, "Common CENC DRMInfo does not have a default KID, cannot convert to %s",
               keySystem.c_str());
      return;
    }
    keyIds.emplace_back(DRM::ConvertKidStrToBytes(drmInfo.defaultKid));
  }

  drmInfo.initData = DRM::PSSH::Make(KeySystemToUUID(keySystem), keyIds);
}

// \brief Query DRM decrypter to get capabilities and set it to session.
// \return True if has success, otherwise false.
bool GetCapabilities(const std::optional<bool> isForceSecureDecoder,
                     const std::optional<bool> drmCfgIsSecureDecoderEnabled,
                     const std::vector<uint8_t>& defaultKid,
                     DRMSession& session)
{
  auto& caps = session.capabilities;
  session.drm->GetCapabilities(session.decrypter, defaultKid, caps, session.mediaType);

  if (caps.flags & DRM::Capabilities::INVALID_STATUS)
  {
    return false;
  }
  else if (caps.flags & DRM::Capabilities::SECURE_PATH)
  {
    // Allow to disable the secure decoder
    bool disableSecureDecoder = CSrvBroker::GetSettings().IsDisableSecureDecoder();
    // but, DRM config can override it
    if (drmCfgIsSecureDecoderEnabled.has_value())
      disableSecureDecoder = !*drmCfgIsSecureDecoderEnabled;
    // but, external config can override all others (e.g. manifest)
    if (isForceSecureDecoder.has_value())
      disableSecureDecoder = !*isForceSecureDecoder;
    if (disableSecureDecoder)
    {
      LOG::Log(LOGDEBUG, "DRM configured with secure decoder disabled");
      caps.flags &= ~DRM::Capabilities::SECURE_DECODER;
    }
  }

  return true;
}

/*
 * \brief Get the union of DRM infos from initialization media segment and manifest
 *        by prioritizing media ones since it should be more accurate than manifest
 * \param mediaDrmInfos The DRM infos from the media
 * \param manifestDrmInfos The DRM infos from the manifest
 * \return The union of DRM infos
 */
std::vector<DRM::DRMInfo> DrmInfosUnion(std::vector<DRM::DRMInfo> mediaDrmInfos,
                                        std::vector<DRM::DRMInfo> manifestDrmInfos)
{
  std::vector<DRM::DRMInfo> drmInfos = mediaDrmInfos;

  for (const auto& item : manifestDrmInfos)
  {
    // Match existing entries by keySystem, defaultKid and initData only.
    // Ignore licenseServerUri so that we can copy it from the manifest
    // when the media-provided entry lacks it.
    auto it = std::find_if(drmInfos.begin(), drmInfos.end(),
                           [&](const DRM::DRMInfo& r)
                           {
                             return r.keySystem == item.keySystem &&
                                    r.defaultKid == item.defaultKid &&
                                    r.initData == item.initData;
                           });

    if (it == drmInfos.end())
    {
      drmInfos.emplace_back(item);
    }
    else
    {
      // If the media entry does not provide a licenseServerUri but the
      // manifest does, copy it. Do not overwrite an existing media URI.
      if (it->licenseServerUri.empty() && !item.licenseServerUri.empty())
        it->licenseServerUri = item.licenseServerUri;
    }
  }

  return drmInfos;
}

void LogDRMInfo(const std::vector<DRM::DRMInfo>& drmInfos)
{
  LOG::Log(LOGDEBUG, "[DRM Info list]");
  for (const auto& drmInfo : drmInfos)
  {
    LOG::Log(LOGDEBUG,
             "[DRM Info] Key system: %s\nDefault KID: %s\nInit data: %s\nCrypto mode: %d\n"
             "License server URI: %s\nServer certification: %s\nRobustness: %s",
             drmInfo.keySystem.c_str(), drmInfo.defaultKid.c_str(),
             BASE64::Encode(drmInfo.initData).c_str(), drmInfo.cryptoMode,
             drmInfo.licenseServerUri.c_str(), BASE64::Encode(drmInfo.serverCert).c_str(),
             drmInfo.robustness.c_str());
  }
}
} // unnamed namespace

bool DRM::CDRMEngine::Initialize()
{
  if (!m_drms.empty())
    return true; // assume as already initialized

  if (m_status == EngineStatus::DRM_ERROR)
    return false; // something wrong with a previous initialization

  //! @todo: to test a way to initialize DRM when manifest is downloaded/parsed
  //! in the hoping to have a more smoother playback transition from unencrypted->to->encrypted periods

  // This is the list of keysystems supported by at least one DRM
  // are ordered by priority where the lower index has higher priority
  // by default ClearKey has the lowest priority since real DRMs should be preferred
  std::vector<std::string_view> keySystemsPrio = {KS_WIDEVINE, KS_PLAYREADY, KS_WISEPLAY, KS_CLEARKEY};

  const auto& kodiProps = CSrvBroker::GetKodiProps();
  // Reorder the keysystems list by using the custom DRM configuration, if any
  for (auto& [ks, cfg] : kodiProps.GetDrmConfigs())
  {
    if (cfg.priority.has_value() && *cfg.priority != 0)
    {
      auto it = std::find(keySystemsPrio.begin(), keySystemsPrio.end(), ks);
      if (it != keySystemsPrio.end())
      {
        keySystemsPrio.erase(it);

        size_t index = *cfg.priority - 1;
        if (index >= keySystemsPrio.size())
          index = keySystemsPrio.size() - 1;

        keySystemsPrio.insert(keySystemsPrio.begin() + index, ks);
      }
    }
  }

  // Get all DRM supported by the platform in use to determine which keysystems are supported
  std::vector<std::shared_ptr<DRM::IDecrypter>> drms = FACTORY::GetDecrypters();

  std::string decrypterPath = CSrvBroker::GetSettings().GetDecrypterPath();
  if (decrypterPath.empty())
  {
    LOG::LogF(LOGERROR,
              "Cannot initialize DrmEngine, no decrypter path set in the add-on settings");
    m_status = EngineStatus::DRM_ERROR;
    return false;
  }

  // Initialize DRMs
  for (auto it = drms.begin(); it != drms.end();)
  {
    (*it)->SetLibraryPath(decrypterPath);

    if (!(*it)->Initialize()) // Failed to initialize DRM, delete it and go on
    {
      LOG::LogF(LOGERROR, "Unable to initialize %s DRM", (*it)->GetName().c_str());
      it = drms.erase(it);
    }
    else
      ++it;
  }

  // Check what keysystems are supported by DRMs by priority order
  // and so add the supported one to the DRM list
  for (std::string_view ks : keySystemsPrio)
  {
    for (auto& drm : drms)
    {
      if (drm->IsKeySystemSupported(ks))
        m_drms.emplace_back(ks, drm);
    }
  }

  if (m_drms.empty())
  {
    LOG::LogF(LOGWARNING, "No DRM available");
    return false;
  }

  return true;
}

bool DRM::CDRMEngine::PreInitializeDRM(DRMSession& session)
{
  auto& kodiProps = CSrvBroker::GetKodiProps();

  const auto propDrmCfg = kodiProps.GetDrmConfig(KS_WIDEVINE);

  if (!propDrmCfg.priority.has_value() || propDrmCfg.priority != 1 || propDrmCfg.preInitData.empty())
    return false;

  if (!Initialize())
    return false;

  // Pre-initialize the DRM is available for Widevine only.
  // Since the manifest will be downloaded later its assumed that
  // the manifest support the DRM and that the priority is set to 1.
  if (!HasKeySystemSupport(KS_WIDEVINE))
    return false;

  LOG::Log(LOGDEBUG, "Pre-initialize crypto session");
  std::vector<uint8_t> initData;
  std::vector<uint8_t> kidData;
  // Parse the init data (PSSH, KID)
  size_t posSplitter = propDrmCfg.preInitData.find("|");
  if (posSplitter != std::string::npos)
  {
    initData = BASE64::Decode(propDrmCfg.preInitData.substr(0, posSplitter));
    kidData = BASE64::Decode(propDrmCfg.preInitData.substr(posSplitter + 1));
  }

  if (initData.empty() || kidData.empty())
  {
    LOG::LogF(LOGERROR, "Invalid \"pre_init_data\" parameter, the data have this format: "
                        "{PSSH as base64}|{KID as base64}");
    m_status = EngineStatus::DRM_ERROR;
    return false;
  }

  m_keySystem = KS_WIDEVINE;

  std::shared_ptr<DRM::IDecrypter> drm = GetDrmInstance(m_keySystem);
  if (!drm)
  {
    m_status = EngineStatus::DRM_ERROR;
    LOG::LogF(LOGERROR, "Cannot get the DRM instance for keysystem %s", m_keySystem.c_str());
    GUI::ErrorDialog(GUI::GetLocalizedString(30303));
    return false;
  }

  LOG::LogF(LOGDEBUG, "Initializing session with KID: %s", STRING::ToHexadecimal(kidData).c_str());

  DRM::Config drmCfg = CreateDRMConfig(m_keySystem, kodiProps.GetDrmConfig(m_keySystem));

  auto dec = drm->CreateSingleSampleDecrypter(drmCfg, kidData, CryptoMode::AES_CTR);

  if (!dec)
  {
    LOG::Log(LOGERROR, "Failed to initialize the %s DRM decrypter", drm->GetName().c_str());
    m_status = EngineStatus::DECRYPTER_ERROR;
    return false;
  }

  const SResult ret = dec->CreateSession(initData, "", true);

  if (ret.IsFailed())
  {
    LOG::LogF(LOGERROR, "Failed to create the DRM session");
    m_status = EngineStatus::DRM_ERROR;
    GUI::ErrorDialog(ret.Message());
    return false;
  }

  session.id = dec->GetSessionId();
  session.challenge = drm->GetChallengeB64Data(dec);
  session.drm = drm;
  session.decrypter = dec;
#ifndef ANDROID
  // On android is not possible add the default KID key used to open DRM
  // then dont add this DRM session, since must be reinitialized
  m_sessions.emplace_back(std::make_shared<DRMSession>(session));
#endif

  m_isPreinitialized = true;

  return true;
}

const std::shared_ptr<DRMSession> DRM::CDRMEngine::InitializeSession(
    std::vector<DRM::DRMInfo> manifestDrmInfos,
    std::vector<DRM::DRMInfo> mediaDrmInfos,
    DRM::DRMMediaType mediaType,
    std::optional<bool> isForceSecureDecoder,
    kodi::addon::InputstreamInfo& streamInfo,
    bool canCleanupSessions)
{
  const auto& kodiProps = CSrvBroker::GetKodiProps();
  
  if (kodiProps.GetManifestConfig().ignoreMediaDefaultKid)
    mediaDrmInfos.clear();

  std::vector<DRM::DRMInfo> drmInfos = DrmInfosUnion(mediaDrmInfos, manifestDrmInfos);

  if (CSrvBroker::GetSettings().IsDebugVerbose())
    LogDRMInfo(drmInfos);

  if (drmInfos.empty())
    return nullptr;

  if (!Initialize())
    return nullptr;

  // Reset status before to start a new initialization
  m_status = EngineStatus::NONE;

  LOG::Log(LOGDEBUG, "Initialize crypto session");

  ConfigureClearKey(drmInfos);

  // This is a kind of hack,
  // some services use manifests (usually SmoothStreaming) with PlayReady DRM only,
  // but they have also a Widevine license server that allow to play same stream with Widevine,
  // this will allow to force change the manifest DRMInfo KeySytem to Widevine and replace the init data
  bool isPRtoWVKeySystem{false};
  if (HasKeySystemSupport(KS_WIDEVINE) && drmInfos.size() == 1 &&
      drmInfos[0].keySystem == KS_PLAYREADY && kodiProps.GetDrmConfigs().size() == 1 &&
      kodiProps.HasDrmConfig(KS_WIDEVINE))
  {
    drmInfos[0].keySystem = KS_WIDEVINE;
    isPRtoWVKeySystem = true;
  }

  if (!SelectDRM(drmInfos))
  {
    LOG::LogF(LOGERROR, "The stream requires an unsupported DRM.");
    GUI::ErrorDialog("The stream requires an unsupported DRM.");
    m_status = EngineStatus::DRM_ERROR;
    return nullptr;
  }

  // Get DRMInfo compatible with key system
  std::vector<DRM::DRMInfo> selDrmInfos = GetDRMInfosByKS(drmInfos, m_keySystem);

  if (selDrmInfos.empty())
  {
    LOG::LogF(LOGERROR, "The Key System \"%s\" does not match any DRMInfo", m_keySystem.c_str());
    m_status = EngineStatus::DRM_ERROR;
    return nullptr;
  }

  const auto drmPropCfg = kodiProps.GetDrmConfig(m_keySystem);
  std::shared_ptr<DRMSession> session{nullptr};

  for (size_t drmInfoIdx = 0; drmInfoIdx < selDrmInfos.size(); ++drmInfoIdx)
  {
    DRM::DRMInfo drmInfo = selDrmInfos[drmInfoIdx];

    ConvertDRMInfoCENC(drmInfo, m_keySystem);

    // Set custom init data PSSH provided from property,
    // can allow to initialize a DRM that could be also not specified
    // as supported in the manifest (e.g. missing DASH ContentProtection tags)
    if (!drmPropCfg.initData.empty() || isPRtoWVKeySystem)
    {
      drmInfo.initData.clear();

      std::vector<uint8_t> customInitData = BASE64::Decode(drmPropCfg.initData);

      if (DRM::IsValidPsshHeader(customInitData))
      {
        LOG::Log(LOGDEBUG, "Use custom init PSSH provided by the \"license\" property");
        drmInfo.initData = customInitData;
      }
      else if (m_keySystem == DRM::KS_WIDEVINE) // Try to create a PSSH box, KID should be provided by manifest
      {
        LOG::Log(LOGDEBUG, "Make a Widevine init PSSH to replace PlayReady init data");
        drmInfo.initData = DRM::PSSH::MakeWidevine({DRM::ConvertKidStrToBytes(drmInfo.defaultKid)},
                                                   customInitData);
      }

      if (drmInfo.initData.empty())
        LOG::LogF(LOGERROR, "The custom init PSSH contains no data");
    }

    // If no KID, but init data, extract the KID from init data
    if (!drmInfo.initData.empty() && drmInfo.defaultKid.empty() &&
        DRM::IsValidPsshHeader(drmInfo.initData))
    {
      LOG::Log(LOGDEBUG, "No default KID provided from DRM info, try extracting from init data");
      DRM::PSSH parser;
      if (parser.Parse(drmInfo.initData))
      {
        const auto& keyIds = parser.GetKeyIds();
        if (keyIds.empty())
          LOG::Log(LOGWARNING, "No KID found in PSSH");
        else if (keyIds.size() > 1)
          LOG::Log(LOGWARNING, "Multiple KIDs found in PSSH, cannot be determined the default");
        else
        {
          LOG::Log(LOGDEBUG, "Default KID parsed from init data");
          drmInfo.defaultKid = STRING::ToLower(STRING::ToHexadecimal(keyIds[0]));
        }
      }
    }

    if (drmInfo.defaultKid.empty())
      LOG::Log(LOGWARNING, "Cannot get default KID from DRM info, decryption can fail");

    std::vector<uint8_t> drmInfoKidBytes = DRM::ConvertKidStrToBytes(drmInfo.defaultKid);

    if (m_isPreinitialized && m_sessions.size() == 1)
    {
      // Widevine only, when the CDM is preinitialized for non-android systems
      // the session has been created with a custom PSSH/KID and it should
      // be assumed that there is a single session for all streams.
      // In order to reuse this session is needed to add the current KID.
      if (!m_sessions[0]->drm->HasLicenseKey(m_sessions[0]->decrypter, drmInfoKidBytes))
      {
        m_sessions[0]->decrypter->AddKeyId(drmInfoKidBytes);
        m_sessions[0]->decrypter->SetDefaultKeyId(drmInfoKidBytes);
      }
    }

    // Check whether it is possible to reuse an existing DRM session
    // its recommended to use separate sessions when a/v media types have different KIDs
    // to avoid possible decryption problems that usually affect android devices (corrupted/pixellated video)
    for (const auto& s : m_sessions)
    {
      bool isReuseSession{false};
      const std::optional<bool> hasKey = s->drm->HasLicenseKey(s->decrypter, drmInfoKidBytes);

      // forced single session, allow to share same session (also with different media types)
      if (drmPropCfg.isForceSingleSession && (!hasKey.has_value() || *hasKey))
      {
        isReuseSession = true;
      }
      // share same session when: KID is the same, otherwise check if there is license key by media type
      else if ((!s->kid.empty() && s->kid == drmInfo.defaultKid) ||
               (hasKey.has_value() && *hasKey && s->mediaType == mediaType))
      {
        isReuseSession = true;
      }
      if (isReuseSession)
      {
        session = s;
        break;
      }
    }

    // No reausable DRM session, create a new one
    if (!session)
    {
      if (canCleanupSessions)
        DeleteSessionsByType(mediaType);

      std::shared_ptr<DRM::IDecrypter> drm = GetDrmInstance(m_keySystem);
      if (!drm)
      {
        m_status = EngineStatus::DRM_ERROR;
        LOG::LogF(LOGERROR, "Cannot get the DRM instance for keysystem %s", m_keySystem.c_str());
        GUI::ErrorDialog(GUI::GetLocalizedString(30303));
        return nullptr;
      }

      DRMSession newSes;
      newSes.drm = drm;
      newSes.mediaType = mediaType;
      newSes.kid = drmInfo.defaultKid;

      DRM::Config drmCfg = DRM::CreateDRMConfig(m_keySystem, drmPropCfg);

      newSes.decrypter = newSes.drm->CreateSingleSampleDecrypter(
          drmCfg, drmInfoKidBytes,
          drmInfo.cryptoMode == CryptoMode::NONE ? CryptoMode::AES_CTR : drmInfo.cryptoMode);

      if (!newSes.decrypter)
      {
        m_status = EngineStatus::DECRYPTER_ERROR;
        LOG::Log(LOGERROR, "Failed to initialize the %s DRM decrypter",
                 newSes.drm->GetName().c_str());
        GUI::ErrorDialog(GUI::GetLocalizedString(30303));
        return nullptr;
      }

      const SResult ret =
          newSes.decrypter->CreateSession(drmInfo.initData, drmInfo.licenseServerUri, false);

      if (ret.IsFailed())
      {
        LOG::LogF(LOGERROR, "Failed to create the DRM session");
        m_status = EngineStatus::DRM_ERROR;
        GUI::ErrorDialog(ret.Message());
        return nullptr;
      }

      const std::optional<bool> licenseHasKey =
          newSes.drm->HasLicenseKey(newSes.decrypter, drmInfoKidBytes);

      const DRM::DRMInfo* altDrmInfo{nullptr};

      // If the DRM license response doesn't contain the requested KID, try to find a fallback
      if (licenseHasKey.has_value() && !*licenseHasKey)
      {
        // Try check if the license contains another valid known KID
        for (size_t nextIdx = drmInfoIdx + 1; nextIdx < selDrmInfos.size(); ++nextIdx)
        {
          const auto& nextDrmInfo = selDrmInfos[nextIdx];

          const std::vector<uint8_t> defaultKidBytes =
              DRM::ConvertKidStrToBytes(nextDrmInfo.defaultKid);

          const std::optional<bool> nextLicenseHasKey =
              newSes.drm->HasLicenseKey(newSes.decrypter, defaultKidBytes);

          // Make sure crypto mode its same
          const CryptoMode cryptoModeA =
              drmInfo.cryptoMode == CryptoMode::NONE ? CryptoMode::AES_CTR : drmInfo.cryptoMode;
          const CryptoMode cryptoModeB = nextDrmInfo.cryptoMode == CryptoMode::NONE
                                             ? CryptoMode::AES_CTR
                                             : nextDrmInfo.cryptoMode;

          if (nextLicenseHasKey.has_value() && *nextLicenseHasKey && cryptoModeA == cryptoModeB)
          {
            LOG::Log(LOGDEBUG, "KID %s not found in DRM license, but found fallback KID %s",
                     drmInfo.defaultKid.c_str(), nextDrmInfo.defaultKid.c_str());

            altDrmInfo = &nextDrmInfo;
            drmInfoKidBytes = defaultKidBytes;
            break;
          }
        }

        if (altDrmInfo) // Found a valid fallback
        {
          newSes.kid = altDrmInfo->defaultKid;
          newSes.decrypter->SetDefaultKeyId(drmInfoKidBytes);
          drmInfo = *altDrmInfo;
        }
        else
        {
          LOG::Log(LOGDEBUG, "KID %s not found in DRM license", drmInfo.defaultKid.c_str());
          continue; // Try request a new license with next DRM info, if available
        }
      }

      newSes.id = newSes.decrypter->GetSessionId();

      if (!GetCapabilities(isForceSecureDecoder, drmPropCfg.isSecureDecoderEnabled, drmInfoKidBytes,
                           newSes))
      {
        m_status = EngineStatus::DECRYPTER_ERROR;
        return nullptr;
      }

      m_sessions.emplace_back(std::make_shared<DRMSession>(newSes));
      session = m_sessions.back();
      LOG::Log(LOGDEBUG, "Initialized new DRM session (ID: %s, KID: %s)", session->id.c_str(),
               drmInfo.defaultKid.c_str());
    }
    else
    {
      // Although we reuse the same session (and decryptor) this create each time a new DRMSession,
      // with the only purpose of differentiating (and caching) the "capabilities", since different KIDs can
      // correspond to different "capabilities", access to capabilities could be improved in the future,
      // perhaps by moving them within the decryptor itself and make an appropriate query inteface so that
      // also other components can query e.g. FragmentedSampleReader, there are potentials to clean various code
      DRMSession newSes;
      newSes.id = session->id;
      newSes.drm = session->drm;
      newSes.decrypter = session->decrypter;
      newSes.mediaType = mediaType;
      newSes.kid = drmInfo.defaultKid;

      if (drmInfo.defaultKid == session->kid) // Same KID same capabilities
      {
        newSes.capabilities = session->capabilities;
      }
      else
      {
        if (!GetCapabilities(isForceSecureDecoder, drmPropCfg.isSecureDecoderEnabled,
                             drmInfoKidBytes, newSes))
        {
          m_status = EngineStatus::DECRYPTER_ERROR;
          return nullptr;
        }
      }

      m_sessions.emplace_back(std::make_shared<DRMSession>(newSes));
      session = m_sessions.back();
      LOG::Log(LOGDEBUG, "Reused existing DRM session (ID: %s, KID: %s)", session->id.c_str(),
               drmInfo.defaultKid.c_str());
    }

    break;
  }

  if (!session)
  {
    LOG::LogF(LOGERROR, "Failed to initialize a DRM session for the stream");
    m_status = EngineStatus::DRM_ERROR;
    return nullptr;
  }

  auto& caps = session->capabilities;

  // Create crypto session
  kodi::addon::StreamCryptoSession cryptoSession;

  cryptoSession.SetSessionId(session->id);
  // Set the key system will enable the crypto session to kodi decoders (e.g. ffmpeg)
  if (caps.flags & DRM::Capabilities::SECURE_PATH)
    cryptoSession.SetKeySystem(KSToCryptoKeySystem(m_keySystem));

  if (caps.flags & DRM::Capabilities::SECURE_PATH &&
      caps.flags & DRM::Capabilities::SUPPORTS_DECODING)
  {
    LOG::Log(LOGDEBUG, "Secure crypto session enabled to DRM session (ID: %s)",
             session->id.c_str());
    streamInfo.SetFeatures(INPUTSTREAM_FEATURE_DECODE);
  }
  else
    streamInfo.SetFeatures(INPUTSTREAM_FEATURE_NONE);

  if (caps.flags & DRM::Capabilities::SECURE_PATH &&
      caps.flags & DRM::Capabilities::SECURE_DECODER)
  {
    // Enable the ISA VideoCodecAdaptive decoder
    LOG::Log(LOGDEBUG, "Secure crypto decoder enabled to DRM session (ID: %s)",
             session->id.c_str());
    cryptoSession.SetFlags(STREAM_CRYPTO_FLAG_SECURE_DECODER);
  }
  else
    cryptoSession.SetFlags(STREAM_CRYPTO_FLAG_NONE);

  streamInfo.SetCryptoSession(cryptoSession);

  return session;
}

const std::shared_ptr<DRMSession> DRM::CDRMEngine::GetSession(const std::string& id) const
{
  if (id.empty())
    return nullptr;

  for (const auto& session : m_sessions)
  {
    if (session->id == id)
      return session;
  }
  return nullptr;
}

const std::shared_ptr<DRMSession> DRM::CDRMEngine::GetSession(const std::string& id, const std::string& kid) const
{
  if (id.empty())
    return nullptr;

  for (const auto& session : m_sessions)
  {
    if (session->id == id && session->kid == kid)
      return session;
  }
  return nullptr;
}

bool DRM::CDRMEngine::ConfigureClearKey(std::vector<DRM::DRMInfo>& drmInfos)
{
  const auto& kodiProps = CSrvBroker::GetKodiProps();

  if (drmInfos.empty())
    return false;

  // Get common info from CENC keysystem or else from first DRMInfo with a default KID
  DRMInfo* baseDrmInfo = GetDRMInfoByKS(drmInfos, "", true);
  if (!baseDrmInfo)
  {
    auto it = std::find_if(drmInfos.begin(), drmInfos.end(),
                           [](const DRM::DRMInfo& s) { return !s.defaultKid.empty(); });
    if (it != drmInfos.end())
      baseDrmInfo = &(*it);
  }

  const CryptoMode commonCryptoMode = baseDrmInfo ? baseDrmInfo->cryptoMode : CryptoMode::NONE;
  const std::string commonDefaultKid = baseDrmInfo ? baseDrmInfo->defaultKid : "";

  // No DRM configuration provided, but the manifest has ClearKey DRMInfo, try force ClearKey usage
  // by removing all other DRMInfo, so that ClearKey will beused to play the stream
  // e.g. HLS that provide initdata with the manifest, so it can be played by default
  if (kodiProps.GetDrmConfigs().empty() &&
      std::any_of(drmInfos.cbegin(), drmInfos.cend(),
                  [](const DRM::DRMInfo& info) { return info.keySystem == KS_CLEARKEY; }))
  {
    drmInfos.erase(std::remove_if(drmInfos.begin(), drmInfos.end(), [](const DRM::DRMInfo& info)
                                  { return info.keySystem != KS_CLEARKEY; }),
                   drmInfos.end());

    DRMInfo* ckDrmInfo = GetDRMInfoByKS(drmInfos, KS_CLEARKEY, true);
    if (ckDrmInfo)
    {
      // Copy missing info
      if (ckDrmInfo->defaultKid.empty())
        ckDrmInfo->defaultKid = commonDefaultKid;
      if (ckDrmInfo->cryptoMode == CryptoMode::NONE)
        ckDrmInfo->cryptoMode = commonCryptoMode;
    }
    return true;
  }

  if (!kodiProps.HasDrmConfig(KS_CLEARKEY))
    return false;

  // At this point we have a ClearKey DRM configuration
  const ADP::KODI_PROPS::DrmCfg& drmCfg = kodiProps.GetDrmConfig(KS_CLEARKEY);

  // Check if custom license data is provided
  const bool isCustomLicense = !drmCfg.license.serverUri.empty() || !drmCfg.license.keys.empty();
  // If no custom license data is provided, we will try to use the ClearKey DRMInfo as is from the manifest
  if (!isCustomLicense)
  {
    DRMInfo* ckDrmInfo = GetDRMInfoByKS(drmInfos, KS_CLEARKEY, true);

    if (ckDrmInfo)
    {
      // Copy missing info
      if (ckDrmInfo->defaultKid.empty())
        ckDrmInfo->defaultKid = commonDefaultKid;
      if (ckDrmInfo->cryptoMode == CryptoMode::NONE)
        ckDrmInfo->cryptoMode = commonCryptoMode;

      // Delete CENC to prevent using it
      drmInfos.erase(std::remove_if(drmInfos.begin(), drmInfos.end(), [](const DRM::DRMInfo& info)
                                    { return info.keySystem.empty(); }),
                     drmInfos.end());
    }

    return true;
  }

  if (kodiProps.GetDrmConfigs().size() == 1) // Single config (CK)
  {
    // Delete all the DRMInfo, so you can force CK even if the manifest uses a different DRM
    drmInfos.clear();
  }
  else // More configs, behavior based on "priority" its needed to preserve DRMInfos
  {
    drmInfos.erase(std::remove_if(drmInfos.begin(), drmInfos.end(), [](const DRM::DRMInfo& info)
                                  { return info.keySystem == KS_CLEARKEY; }),
                   drmInfos.end());
  }

  // Create a custom DRM info for ClearKey

  std::string licenseUri;

  if (drmCfg.license.keys.empty())
  {
    licenseUri = drmCfg.license.serverUri;
  }
  else // Create license uri with jwkSets
  {
    njson jData;
    njson jwkSets = njson::array();

    for (auto& [kid, key] : drmCfg.license.keys)
    {
      const std::string kVal =
          BASE64::UrlSafeEncode(BASE64::Encode(DRM::ConvertKidStrToBytes(key), false));
      const std::string kidVal =
          BASE64::UrlSafeEncode(BASE64::Encode(DRM::ConvertKidStrToBytes(kid), false));

      njson jwkSet;
      jwkSet["k"] = kVal;
      jwkSet["kid"] = kidVal;
      jwkSet["kty"] = "oct";
      jwkSets.push_back(jwkSet);
    }

    jData["keys"] = jwkSets;
    jData["type"] = "temporary";

    const std::string dumps = jData.dump(-1, ' ', false, njson::error_handler_t::ignore);

    licenseUri = "data:application/json;base64," + BASE64::Encode(dumps);
  }

  DRM::DRMInfo drmInfo;
  drmInfo.keySystem = KS_CLEARKEY;
  drmInfo.cryptoMode = commonCryptoMode;
  drmInfo.defaultKid = commonDefaultKid;
  drmInfo.licenseServerUri = licenseUri;
  drmInfos.emplace_back(drmInfo);

  return true;
}

bool DRM::CDRMEngine::SelectDRM(std::vector<DRM::DRMInfo>& drmInfos)
{
  if (!m_keySystem.empty())
    return true;

  // Iterate supported DRM Key System's to find a match with the drmInfo's,
  // the supported DRM's are ordered by priority
  // the lower index have the higher priority
  for (auto& drm : m_drms)
  {
    const DRM::DRMInfo* drmInfo = GetDRMInfoByKS(drmInfos, drm.keySystem);

    if (drmInfo)
    {
      m_keySystem = drm.keySystem;
      LOG::LogF(LOGDEBUG, "Selected DRM key system: %s", m_keySystem.c_str());
      break;
    }
  }

  return !m_keySystem.empty();
}

void DRM::CDRMEngine::DeleteSessionsByType(const DRMMediaType mediaType)
{
  // Despite this will delete sessions, the shared IDecrypter/Adaptive_CencSingleSampleDecrypter
  // might still be in use, for example on CVideoCodecAdaptive, so shared uses can be deleted at later time
  m_sessions.erase(std::remove_if(m_sessions.begin(), m_sessions.end(),
                                  [mediaType](const std::shared_ptr<DRMSession>& session)
                                  { return session->mediaType == mediaType; }),
                   m_sessions.end());
}

bool DRM::CDRMEngine::HasKeySystemSupport(std::string_view keySystem) const
{
  return std::any_of(m_drms.cbegin(), m_drms.cend(),
                     [&keySystem](const DRMInstance& a) { return a.keySystem == keySystem; });
}

std::shared_ptr<DRM::IDecrypter> DRM::CDRMEngine::GetDrmInstance(std::string_view ks) const
{
  auto it = std::find_if(m_drms.cbegin(), m_drms.cend(),
                         [&ks](const DRMInstance& d) { return d.keySystem == ks; });
  return it != m_drms.cend() ? it->drm : nullptr;
}

void DRM::CDRMEngine::Dispose()
{
  LOG::Log(LOGDEBUG, "Dispose DRM Engine");
  m_sessions.clear();
  m_drms.clear();
}
