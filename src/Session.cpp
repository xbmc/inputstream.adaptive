/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Session.h"

#include "aes_decrypter.h"
#include "common/AdaptiveTreeFactory.h"
#include "common/Chooser.h"
#include "samplereader/ADTSSampleReader.h"
#include "samplereader/FragmentedSampleReader.h"
#include "samplereader/SubtitleSampleReader.h"
#include "samplereader/TSSampleReader.h"
#include "samplereader/WebmSampleReader.h"
#include "utils/Base64Utils.h"
#include "utils/CurlUtils.h"
#include "utils/SettingsUtils.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <array>

#include <kodi/addon-instance/Inputstream.h>

using namespace kodi::tools;
using namespace PLAYLIST;
using namespace SESSION;
using namespace UTILS;

CSession::CSession(const PROPERTIES::KodiProperties& kodiProps,
                   const std::string& manifestUrl,
                   const std::string& profilePath)
  : m_kodiProps(kodiProps),
    m_manifestUrl(manifestUrl),
    m_KodiHost(std::make_unique<CKodiHost>()),
    m_reprChooser(CHOOSER::CreateRepresentationChooser(kodiProps))
{
  m_KodiHost->SetProfilePath(profilePath);
  m_KodiHost->SetDebugSaveLicense(kodi::addon::GetSettingBoolean("debug.save.license"));

  m_settingNoSecureDecoder = kodi::addon::GetSettingBoolean("NOSECUREDECODER");
  LOG::Log(LOGDEBUG, "Setting NOSECUREDECODER value: %d", m_settingNoSecureDecoder);

  m_settingIsHdcpOverride = kodi::addon::GetSettingBoolean("HDCPOVERRIDE");
  LOG::Log(LOGDEBUG, "Ignore HDCP status setting value: %i", m_settingIsHdcpOverride);

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

  if (!kodiProps.m_serverCertificate.empty())
  {
    std::string decCert{BASE64::Decode(kodiProps.m_serverCertificate)};
    m_serverCertificate.SetData(reinterpret_cast<const AP4_Byte*>(decCert.data()), decCert.size());
  }
}

CSession::~CSession()
{
  LOG::Log(LOGDEBUG, "CSession::~CSession()");
  m_streams.clear();
  DisposeDecrypter();

  delete m_adaptiveTree;
  m_adaptiveTree = nullptr;

  delete m_reprChooser;
  m_reprChooser = nullptr;
}

void CSession::SetSupportedDecrypterURN(std::string& key_system)
{
  typedef SSD::SSD_DECRYPTER* (*CreateDecryptorInstanceFunc)(SSD::SSD_HOST * host,
                                                             uint32_t version);

  std::string specialpath = kodi::addon::GetSettingString("DECRYPTERPATH");
  if (specialpath.empty())
  {
    LOG::Log(LOGDEBUG, "DECRYPTERPATH not specified in settings.xml");
    return;
  }
  m_KodiHost->SetLibraryPath(kodi::vfs::TranslateSpecialProtocol(specialpath).c_str());

  std::array<std::string, 3> searchPaths =
  {
    kodi::vfs::TranslateSpecialProtocol("special://xbmcbinaddons/inputstream.adaptive/"),
    kodi::vfs::TranslateSpecialProtocol("special://xbmcaltbinaddons/inputstream.adaptive/"),
    kodi::addon::GetAddonInfo("path"),
  };

  std::vector<kodi::vfs::CDirEntry> items;

  for (auto searchPath : searchPaths)
  {
    LOG::Log(LOGDEBUG, "Searching for decrypters in: %s", searchPath.c_str());

    if (!kodi::vfs::GetDirectory(searchPath, "", items))
      continue;

    for (auto item : items)
    {
      if (item.Label().compare(0, 4, "ssd_") && item.Label().compare(0, 7, "libssd_"))
        continue;

      bool success = false;
      m_dllHelper = std::make_unique<kodi::tools::CDllHelper>();
      if (m_dllHelper->LoadDll(item.Path()))
      {
#if defined(__linux__) && defined(__aarch64__) && !defined(ANDROID)
        // On linux arm64, libwidevinecdm.so depends on two dynamic symbols:
        //   __aarch64_ldadd4_acq_rel
        //   __aarch64_swp4_acq_rel
        // These are defined in libssd_wv.so, but to make them available in the main binary's PLT,
        // we need RTLD_GLOBAL. LoadDll() above uses RTLD_LOCAL, so we use RTLD_NOLOAD here to
        // switch the flags from LOCAL to GLOBAL.
        void *hdl = dlopen(item.Path().c_str(), RTLD_NOLOAD | RTLD_GLOBAL | RTLD_LAZY);
        if (!hdl)
        {
          LOG::Log(LOGERROR, "Failed to reload dll in global mode: %s", dlerror());
        }
#endif
        CreateDecryptorInstanceFunc startup;
        if (m_dllHelper->RegisterSymbol(startup, "CreateDecryptorInstance"))
        {
          SSD::SSD_DECRYPTER* decrypter = startup(m_KodiHost.get(), SSD::SSD_HOST::version);
          const char* suppUrn(0);

          if (decrypter && (suppUrn = decrypter->SelectKeySytem(m_kodiProps.m_licenseType.c_str())))
          {
            LOG::Log(LOGDEBUG, "Found decrypter: %s", item.Path().c_str());
            success = true;
            m_decrypter = decrypter;
            key_system = suppUrn;
            break;
          }
        }
      }
      else
      {
        LOG::Log(LOGDEBUG, "%s", dlerror());
      }
      if (!success)
      {
        m_dllHelper.reset();
      }
    }
  }
}

void CSession::DisposeSampleDecrypter()
{
  if (m_decrypter)
  {
    for (auto& cdmSession : m_cdmSessions)
    {
      cdmSession.m_cdmSessionStr = nullptr;
      if (!cdmSession.m_sharedCencSsd)
      {
        m_decrypter->DestroySingleSampleDecrypter(cdmSession.m_cencSingleSampleDecrypter);
        cdmSession.m_cencSingleSampleDecrypter = nullptr;
      }
      else
      {
        cdmSession.m_cencSingleSampleDecrypter = nullptr;
        cdmSession.m_sharedCencSsd = false;
      }
    }
  }
}

void CSession::DisposeDecrypter()
{
  if (!m_dllHelper)
    return;

  DisposeSampleDecrypter();

  typedef void (*DeleteDecryptorInstanceFunc)(SSD::SSD_DECRYPTER*);
  DeleteDecryptorInstanceFunc disposefn;

  if (m_dllHelper->RegisterSymbol(disposefn, "DeleteDecryptorInstance"))
    disposefn(m_decrypter);

  m_decrypter = nullptr;
}

/*----------------------------------------------------------------------
|   initialize
+---------------------------------------------------------------------*/

bool CSession::Initialize()
{
  // Get URN's wich are supported by this addon
  std::string supportedKeySystem;
  if (!m_kodiProps.m_licenseType.empty())
  {
    SetSupportedDecrypterURN(supportedKeySystem);
    LOG::Log(LOGDEBUG, "Supported URN: %s", supportedKeySystem.c_str());
  }

  // Preinitialize the DRM, if pre-initialisation data are provided
  std::map<std::string, std::string> addHeaders;
  bool isSessionOpened{false};

  if (!m_kodiProps.m_drmPreInitData.empty())
  {
    std::string challengeB64;
    std::string sessionId;
    // Pre-initialize the DRM allow to generate the challenge and session ID data
    // used to make licensed manifest requests (via proxy callback)
    if (PreInitializeDRM(challengeB64, sessionId, isSessionOpened))
    {
      addHeaders["challengeB64"] = STRING::URLEncode(challengeB64);
      addHeaders["sessionId"] = sessionId;
    }
    else
    {
      return false;
    }
  }

  std::string manifestUrl = m_manifestUrl;

  //! @todo: In the next version of kodi, remove this hack of adding the $START_NUMBER$ parameter
  //!        to the manifest url which is forcibly cut and copied to the manifest update request url,
  //!        this seem used by YouTube addon only, adaptations are relatively simple
  std::string manifestUpdateParam = m_kodiProps.m_manifestUpdateParam;
  if (manifestUpdateParam.empty() && STRING::Contains(manifestUrl, "$START_NUMBER$"))
  {
    LOG::Log(LOGWARNING,
             "The misuse of adding params with $START_NUMBER$ placeholder to the "
             "manifest url has been deprecated and will be removed on next Kodi version.\n"
             "Please use \"manifest_update_parameter\" Kodi property to set manifest update "
             "parameters, see Wiki integration page.");
    manifestUpdateParam = URL::GetParametersFromPlaceholder(manifestUrl, "$START_NUMBER$");
    manifestUrl.resize(manifestUrl.size() - manifestUpdateParam.size());
  }

  CURL::HTTPResponse manifestResp;
  if (!CURL::DownloadFile(manifestUrl, addHeaders, {"etag", "last-modified"}, manifestResp))
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

  m_adaptiveTree = PLAYLIST_FACTORY::CreateAdaptiveTree(m_kodiProps, manifestResp);
  if (!m_adaptiveTree)
    return false;

  m_adaptiveTree->Configure(m_kodiProps, m_reprChooser, supportedKeySystem, manifestUpdateParam);

  if (!m_adaptiveTree->Open(manifestResp.effectiveUrl, manifestResp.headers, manifestResp.data))
  {
    LOG::Log(LOGERROR, "Cannot parse the manifest (%s)", manifestUrl.c_str());
    return false;
  }

  m_adaptiveTree->PostOpen(m_kodiProps);

  return InitializePeriod(isSessionOpened);
}

void CSession::CheckHDCP()
{
  //! @todo: is needed to implement an appropriate CP check to
  //! remove HDCPOVERRIDE setting workaround
  if (m_cdmSessions.empty())
    return;

  std::vector<SSD::SSD_DECRYPTER::SSD_CAPS> decrypterCaps;

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

      const SSD::SSD_DECRYPTER::SSD_CAPS& ssd_caps = decrypterCaps[repr->m_psshSetPos];

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
  std::string psshData;
  std::string kidData;
  // Parse the PSSH/KID data
  std::string::size_type posSplitter(m_kodiProps.m_drmPreInitData.find("|"));
  if (posSplitter != std::string::npos)
  {
    psshData = m_kodiProps.m_drmPreInitData.substr(0, posSplitter);
    kidData = m_kodiProps.m_drmPreInitData.substr(posSplitter + 1);
  }

  if (psshData.empty() || kidData.empty())
  {
    LOG::LogF(LOGERROR, "Invalid DRM pre-init data, must be as: {PSSH as base64}|{KID as base64}");
    return false;
  }

  m_cdmSessions.resize(2);
  memset(&m_cdmSessions.front(), 0, sizeof(CCdmSession));
  // Try to initialize an SingleSampleDecryptor
  LOG::LogF(LOGDEBUG, "Entering encryption section");

  if (m_kodiProps.m_licenseKey.empty())
  {
    LOG::LogF(LOGERROR, "Invalid license_key");
    return false;
  }

  if (!m_decrypter)
  {
    LOG::LogF(LOGERROR, "No decrypter found for encrypted stream");
    return false;
  }

  if (!m_decrypter->HasCdmSession())
  {
    if (!m_decrypter->OpenDRMSystem(m_kodiProps.m_licenseKey.c_str(), m_serverCertificate,
                                    m_drmConfig))
    {
      LOG::LogF(LOGERROR, "OpenDRMSystem failed");
      return false;
    }
  }

  AP4_DataBuffer init_data;
  init_data.SetBufferSize(1024);
  const char* optionalKeyParameter{nullptr};

  // Set the provided PSSH
  std::string decPssh{BASE64::Decode(psshData)};
  init_data.SetData(reinterpret_cast<const AP4_Byte*>(decPssh.data()), decPssh.size());

  // Decode the provided KID
  std::string decKid{BASE64::Decode(kidData)};

  CCdmSession& session(m_cdmSessions[1]);

  std::string hexKid{StringUtils::ToHexadecimal(decKid)};
  LOG::LogF(LOGDEBUG, "Initializing session with KID: %s", hexKid.c_str());

  if (m_decrypter && init_data.GetDataSize() >= 4 &&
      (session.m_cencSingleSampleDecrypter = m_decrypter->CreateSingleSampleDecrypter(
           init_data, optionalKeyParameter, decKid, true, CryptoMode::AES_CTR)) != 0)
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
  memset(&m_cdmSessions.front(), 0, sizeof(CCdmSession));

  // Try to initialize an SingleSampleDecryptor
  if (m_adaptiveTree->m_currentPeriod->GetEncryptionState() !=
      EncryptionState::UNENCRYPTED)
  {
    std::string licenseKey{m_kodiProps.m_licenseKey};

    if (licenseKey.empty())
      licenseKey = m_adaptiveTree->GetLicenseUrl();

    LOG::Log(LOGDEBUG, "Entering encryption section");

    if (licenseKey.empty())
    {
      LOG::Log(LOGERROR, "Invalid license_key");
      return false;
    }

    if (!m_decrypter)
    {
      LOG::Log(LOGERROR, "No decrypter found for encrypted stream");
      return false;
    }

    if (!m_decrypter->HasCdmSession())
    {
      if (!m_decrypter->OpenDRMSystem(licenseKey.c_str(), m_serverCertificate, m_drmConfig))
      {
        LOG::Log(LOGERROR, "OpenDRMSystem failed");
        return false;
      }
    }
    std::string strkey(m_adaptiveTree->m_supportedKeySystem.substr(9));
    size_t pos;
    while ((pos = strkey.find('-')) != std::string::npos)
      strkey.erase(pos, 1);
    if (strkey.size() != 32)
    {
      LOG::Log(LOGERROR, "Key system mismatch (%s)!", m_adaptiveTree->m_supportedKeySystem.c_str());
      return false;
    }

    unsigned char key_system[16];
    AP4_ParseHex(strkey.c_str(), key_system, 16);

    // cdmSession 0 is reserved for unencrypted streams
    for (size_t ses{1}; ses < m_cdmSessions.size(); ++ses)
    {
      AP4_DataBuffer init_data;
      const char* optionalKeyParameter{nullptr};

      CPeriod::PSSHSet& sessionPsshset = m_adaptiveTree->m_currentPeriod->GetPSSHSets()[ses];

      if (sessionPsshset.pssh_ == PSSH_FROM_FILE)
      {
        LOG::Log(LOGDEBUG, "Searching PSSH data in FILE");

        if (m_kodiProps.m_licenseData.empty())
        {
          auto initialRepr{m_reprChooser->GetRepresentation(sessionPsshset.adaptation_set_)};

          CStream stream{*m_adaptiveTree, sessionPsshset.adaptation_set_, initialRepr, m_kodiProps,
                         false};

          stream.m_isEnabled = true;
          stream.m_adStream.start_stream();
          stream.SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream.m_adStream));
          stream.SetStreamFile(std::make_unique<AP4_File>(*stream.GetAdByteStream(),
                                                          AP4_DefaultAtomFactory::Instance_, true));
          AP4_Movie* movie{stream.GetStreamFile()->GetMovie()};
          if (movie == NULL)
          {
            LOG::Log(LOGERROR, "No MOOV in stream!");
            stream.Disable();
            return false;
          }
          AP4_Array<AP4_PsshAtom>& pssh{movie->GetPsshAtoms()};

          for (size_t i{0}; !init_data.GetDataSize() && i < pssh.ItemCount(); i++)
          {
            if (memcmp(pssh[i].GetSystemId(), key_system, 16) == 0)
            {
              init_data.AppendData(pssh[i].GetData().GetData(), pssh[i].GetData().GetDataSize());
              if (sessionPsshset.defaultKID_.empty())
              {
                if (pssh[i].GetKid(0))
                {
                  sessionPsshset.defaultKID_ = std::string((const char*)pssh[i].GetKid(0), 16);
                }
                else if (AP4_Track* track = movie->GetTrack(
                             static_cast<AP4_Track::Type>(stream.m_adStream.GetTrackType())))
                {
                  AP4_ProtectedSampleDescription* m_protectedDesc =
                      static_cast<AP4_ProtectedSampleDescription*>(track->GetSampleDescription(0));
                  AP4_ContainerAtom* schi;
                  if (m_protectedDesc->GetSchemeInfo() &&
                      (schi = m_protectedDesc->GetSchemeInfo()->GetSchiAtom()))
                  {
                    AP4_TencAtom* tenc{
                        AP4_DYNAMIC_CAST(AP4_TencAtom, schi->GetChild(AP4_ATOM_TYPE_TENC, 0))};
                    if (tenc)
                    {
                      sessionPsshset.defaultKID_ =
                          std::string(reinterpret_cast<const char*>(tenc->GetDefaultKid()), 16);
                    }
                    else
                    {
                      AP4_PiffTrackEncryptionAtom* piff{
                          AP4_DYNAMIC_CAST(AP4_PiffTrackEncryptionAtom,
                                           schi->GetChild(AP4_UUID_PIFF_TRACK_ENCRYPTION_ATOM, 0))};
                      if (piff)
                      {
                        sessionPsshset.defaultKID_ =
                            std::string(reinterpret_cast<const char*>(piff->GetDefaultKid()), 16);
                      }
                    }
                  }
                }
              }
            }
          }

          if (!init_data.GetDataSize())
          {
            LOG::Log(LOGERROR, "Could not extract license from video stream (PSSH not found)");
            stream.Disable();
            return false;
          }
          stream.Disable();
        }
        else if (!sessionPsshset.defaultKID_.empty())
        {
          std::string licenseData = BASE64::Decode(m_kodiProps.m_licenseData);
          // Replace KID placeholder, if any
          STRING::ReplaceFirst(licenseData, "{KID}", sessionPsshset.defaultKID_);

          init_data.SetData(reinterpret_cast<const AP4_Byte*>(licenseData.c_str()),
                            static_cast<AP4_Size>(licenseData.size()));
        }
        else
          return false;
      }
      else
      {
        if (m_kodiProps.m_manifestType == PROPERTIES::ManifestType::ISM)
        {
          if (m_kodiProps.m_licenseType == "com.widevine.alpha")
          {
            std::string licenseData{m_kodiProps.m_licenseData};
            if (licenseData.empty())
              licenseData = "e0tJRH0="; // {KID}
            std::vector<uint8_t> init_data_v;
            CreateISMlicense(sessionPsshset.defaultKID_, licenseData, init_data_v);
            init_data.SetData(init_data_v.data(), init_data_v.size());
          }
          else
          {
            init_data.SetData(reinterpret_cast<const uint8_t*>(sessionPsshset.pssh_.data()),
                              sessionPsshset.pssh_.size());
            optionalKeyParameter =
                m_kodiProps.m_licenseData.empty() ? nullptr : m_kodiProps.m_licenseData.c_str();
          }
        }
        else
        {
          std::string decPssh{BASE64::Decode(sessionPsshset.pssh_)};
          init_data.SetBufferSize(1024);
          init_data.SetData(reinterpret_cast<const AP4_Byte*>(decPssh.data()), decPssh.size());
        }
      }

      CCdmSession& session{m_cdmSessions[ses]};
      std::string defaultKid{sessionPsshset.defaultKID_};
      const uint8_t* defkid{
          defaultKid.empty() ? nullptr : reinterpret_cast<const uint8_t*>(defaultKid.data())};

      if (addDefaultKID && ses == 1 && session.m_cencSingleSampleDecrypter)
      {
        // If the CDM has been pre-initialized, on non-android systems
        // we use the same session opened then we have to add the current KID
        // because the session has been opened with a different PSSH/KID
        session.m_cencSingleSampleDecrypter->AddKeyId(defaultKid);
        session.m_cencSingleSampleDecrypter->SetDefaultKeyId(defaultKid);
      }

      if (m_decrypter && !defaultKid.empty())
      {
        std::string hexKid{StringUtils::ToHexadecimal(defaultKid)};
        LOG::Log(LOGDEBUG, "Initializing stream with KID: %s", hexKid.c_str());

        for (size_t i{1}; i < ses; ++i)
        {
          if (m_decrypter->HasLicenseKey(m_cdmSessions[i].m_cencSingleSampleDecrypter, defkid))
          {
            session.m_cencSingleSampleDecrypter = m_cdmSessions[i].m_cencSingleSampleDecrypter;
            session.m_sharedCencSsd = true;
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
            session.m_sharedCencSsd = true;
            break;
          }
        }
        if (!session.m_cencSingleSampleDecrypter)
        {
          LOG::Log(LOGWARNING, "Initializing stream with unknown KID!");
        }
      }

      if (m_decrypter && init_data.GetDataSize() >= 4 &&
          (session.m_cencSingleSampleDecrypter ||
           (session.m_cencSingleSampleDecrypter = m_decrypter->CreateSingleSampleDecrypter(
                init_data, optionalKeyParameter, defaultKid, false,
                sessionPsshset.m_cryptoMode == CryptoMode::NONE ? CryptoMode::AES_CTR
                                                                : sessionPsshset.m_cryptoMode)) !=
               0))
      {
        m_decrypter->GetCapabilities(session.m_cencSingleSampleDecrypter, defkid,
                                     sessionPsshset.media_, session.m_decrypterCaps);

        if (session.m_decrypterCaps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_INVALID)
        {
          m_adaptiveTree->m_currentPeriod->RemovePSSHSet(static_cast<std::uint16_t>(ses));
        }
        else if (session.m_decrypterCaps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH)
        {
          session.m_cdmSessionStr = session.m_cencSingleSampleDecrypter->GetSessionId();
          isSecureVideoSession = true;

          if (m_settingNoSecureDecoder && !m_kodiProps.m_isLicenseForceSecureDecoder &&
              !m_adaptiveTree->m_currentPeriod->IsSecureDecodeNeeded())
            session.m_decrypterCaps.flags &= ~SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_DECODER;
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

  if (!m_settingIsHdcpOverride)
    CheckHDCP();

  m_reprChooser->SetSecureSession(isSecureVideoSession);
  m_reprChooser->PostInit();

  return true;
}

bool CSession::InitializePeriod(bool isSessionOpened /* = false */)
{
  bool psshChanged{true};
  if (m_adaptiveTree->m_nextPeriod)
  {
    psshChanged =
        !(m_adaptiveTree->m_currentPeriod->GetPSSHSets() == m_adaptiveTree->m_nextPeriod->GetPSSHSets());
    m_adaptiveTree->m_currentPeriod = m_adaptiveTree->m_nextPeriod;
    m_adaptiveTree->m_nextPeriod = nullptr;
  }

  m_chapterStartTime = GetChapterStartTime();

  if (m_adaptiveTree->m_currentPeriod->GetEncryptionState() == EncryptionState::ENCRYPTED)
  {
    LOG::LogF(LOGERROR, "Unhandled encrypted stream.");
    return false;
  }

  // create SESSION::STREAM objects. One for each AdaptationSet
  m_streams.clear();

  if (!psshChanged)
    LOG::Log(LOGDEBUG, "Reusing DRM psshSets for new period!");
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
  SETTINGS::StreamSelection streamSelectionMode{m_reprChooser->GetStreamSelectionMode()};

  while ((adp = m_adaptiveTree->GetAdaptationSet(adpIndex++)))
  {
    if (adp->GetRepresentations().empty())
      continue;

    bool isManualStreamSelection;
    if (adp->GetStreamType() == StreamType::VIDEO)
      isManualStreamSelection = streamSelectionMode != SETTINGS::StreamSelection::AUTO;
    else
      isManualStreamSelection = streamSelectionMode == SETTINGS::StreamSelection::MANUAL;

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

        AddStream(adp, currentRepr, isDefaultRepr, uniqueId);
      }
    }
    else
    {
      // Add the default stream representation only
      size_t reprIndex{adp->GetRepresentations().size()};
      uint32_t uniqueId{adpIndex};
      uniqueId |= reprIndex << 16;

      AddStream(adp, defaultRepr, true, uniqueId);
    }
  }

  m_firstPeriodInitialized = true;
  return true;
}

void CSession::AddStream(PLAYLIST::CAdaptationSet* adp,
                         PLAYLIST::CRepresentation* initialRepr,
                         bool isDefaultRepr,
                         uint32_t uniqueId)
{
  m_streams.push_back(std::make_unique<CStream>(*m_adaptiveTree, adp, initialRepr, m_kodiProps,
                                                m_firstPeriodInitialized));

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
      if (adp->IsOriginal())
        flags |= INPUTSTREAM_FLAG_ORIGINAL;
      if (adp->IsDefault())
        flags |= INPUTSTREAM_FLAG_DEFAULT;
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
    std::string annexb;
    const std::string* extraData(&annexb);

    const SSD::SSD_DECRYPTER::SSD_CAPS& caps{GetDecrypterCaps(rep->m_psshSetPos)};

    if ((caps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_ANNEXB_REQUIRED) &&
        stream.m_info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
    {
      LOG::Log(LOGDEBUG, "UpdateStream: Convert avc -> annexb");
      annexb = AvcToAnnexb(rep->GetCodecPrivateData());
    }
    else
    {
      extraData = &rep->GetCodecPrivateData();
    }
    stream.m_info.SetExtraData(reinterpret_cast<const uint8_t*>(extraData->c_str()),
                               extraData->size());
  }

  stream.m_info.SetCodecFourCC(0);
  stream.m_info.SetBitRate(rep->GetBandwidth());

  // Original codec name
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

    if (rep->ContainsCodec("avc", codecStr) || rep->ContainsCodec("h264", codecStr))
      stream.m_info.SetCodecName("h264");
    else if (rep->ContainsCodec("hev", codecStr))
      stream.m_info.SetCodecName("hevc");
    else if (rep->ContainsCodec("hvc", codecStr) || rep->ContainsCodec("dvh", codecStr))
    {
      stream.m_info.SetCodecFourCC(MakeFourCC(codecStr[0], codecStr[1], codecStr[2], codecStr[3]));
      stream.m_info.SetCodecName("hevc");
    }
    else if (rep->ContainsCodec("vp9", codecStr) || rep->ContainsCodec("vp09", codecStr))
    {
      stream.m_info.SetCodecName("vp9");
      if (STRING::Contains(codecStr, "."))
      {
        int codecProfileNum = STRING::ToInt32(codecStr.substr(codecStr.find('.') + 1));
        switch (codecProfileNum)
        {
          case 0:
            stream.m_info.SetCodecProfile(VP9CodecProfile0);
            break;
          case 1:
            stream.m_info.SetCodecProfile(VP9CodecProfile1);
            break;
          case 2:
            stream.m_info.SetCodecProfile(VP9CodecProfile2);
            break;
          case 3:
            stream.m_info.SetCodecProfile(VP9CodecProfile3);
            break;
          default:
            LOG::LogF(LOGWARNING, "Unhandled video codec profile \"%i\" for codec string: %s",
                      codecProfileNum, codecStr.c_str());
            break;
        }
      }
    }
    else if (rep->ContainsCodec("av1", codecStr) || rep->ContainsCodec("av01", codecStr))
      stream.m_info.SetCodecName("av1");
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

    if (rep->ContainsCodec("mp4a", codecStr) || rep->ContainsCodec("aac", codecStr))
      stream.m_info.SetCodecName("aac");
    else if (rep->ContainsCodec("dts", codecStr))
      stream.m_info.SetCodecName("dca");
    else if (rep->ContainsCodec("ac-3", codecStr))
      stream.m_info.SetCodecName("ac3");
    else if (rep->ContainsCodec("ec-3", codecStr))
      stream.m_info.SetCodecName("eac3");
    else if (rep->ContainsCodec("opus", codecStr))
      stream.m_info.SetCodecName("opus");
    else if (rep->ContainsCodec("vorbis", codecStr))
      stream.m_info.SetCodecName("vorbis");
    else
    {
      stream.m_isValid = false;
      LOG::LogF(LOGERROR, "Unhandled audio codec");
    }
  }
  else if (streamType == StreamType::SUBTITLE)
  {
    if (rep->ContainsCodec("stpp", codecStr) || rep->ContainsCodec("ttml", codecStr))
      stream.m_info.SetCodecName("srt");
    else if (rep->ContainsCodec("wvtt", codecStr))
      stream.m_info.SetCodecName("webvtt");
    else
    {
      stream.m_isValid = false;
      LOG::LogF(LOGERROR, "Unhandled subtitle codec");
    }
  }

  stream.m_info.SetCodecInternalName(codecStr);
}

AP4_Movie* CSession::PrepareStream(CStream* stream, bool& needRefetch)
{
  needRefetch = false;
  CRepresentation* repr = stream->m_adStream.getRepresentation();

  switch (m_adaptiveTree->prepareRepresentation(stream->m_adStream.getPeriod(),
                                                stream->m_adStream.getAdaptationSet(), repr))
  {
    case PrepareRepStatus::FAILURE:
      return nullptr;
    case PrepareRepStatus::DRMCHANGED:
      if (!InitializeDRM())
        return nullptr;
      [[fallthrough]];
    case PrepareRepStatus::DRMUNCHANGED:
      stream->m_isEncrypted = repr->m_psshSetPos != PSSHSET_POS_DEFAULT;
      needRefetch = true;
      break;
    default:
      break;
  }

  if (repr->GetContainerType() == ContainerType::MP4 && !repr->HasInitPrefixed() &&
      !repr->HasInitialization())
  {
    //We'll create a Movie out of the things we got from manifest file
    //note: movie will be deleted in destructor of stream->input_file_
    AP4_Movie* movie{new AP4_Movie()};

    AP4_SyntheticSampleTable* sample_table{new AP4_SyntheticSampleTable()};

    AP4_SampleDescription* sample_descryption;
    const std::string& extradata = repr->GetCodecPrivateData();

    if (stream->m_info.GetCodecName() == "h264")
    {
      AP4_MemoryByteStream ms{reinterpret_cast<const uint8_t*>(extradata.data()),
                              static_cast<const AP4_Size>(extradata.size())};
      AP4_AvccAtom* atom{AP4_AvccAtom::Create(AP4_ATOM_HEADER_SIZE + extradata.size(), ms)};
      sample_descryption =
          new AP4_AvcSampleDescription(AP4_SAMPLE_FORMAT_AVC1, stream->m_info.GetWidth(),
                                       stream->m_info.GetHeight(), 0, nullptr, atom);
    }
    else if (stream->m_info.GetCodecName() == "hevc")
    {
      AP4_MemoryByteStream ms{reinterpret_cast<const AP4_UI08*>(extradata.data()),
                              static_cast<const AP4_Size>(extradata.size())};
      AP4_HvccAtom* atom{AP4_HvccAtom::Create(AP4_ATOM_HEADER_SIZE + extradata.size(), ms)};
      sample_descryption =
          new AP4_HevcSampleDescription(AP4_SAMPLE_FORMAT_HEV1, stream->m_info.GetWidth(),
                                        stream->m_info.GetHeight(), 0, nullptr, atom);
    }
    else if (stream->m_info.GetCodecName() == "av1")
    {
      AP4_MemoryByteStream ms{reinterpret_cast<const AP4_UI08*>(extradata.data()),
                              static_cast<AP4_Size>(extradata.size())};
      AP4_Av1cAtom* atom = AP4_Av1cAtom::Create(AP4_ATOM_HEADER_SIZE + extradata.size(), ms);
      sample_descryption =
          new AP4_Av1SampleDescription(AP4_SAMPLE_FORMAT_AV01, stream->m_info.GetWidth(),
                                       stream->m_info.GetHeight(), 0, nullptr, atom);
    }
    else if (stream->m_info.GetCodecName() == "srt")
      sample_descryption = new AP4_SampleDescription(AP4_SampleDescription::TYPE_SUBTITLES,
                                                     AP4_SAMPLE_FORMAT_STPP, 0);
    else
      sample_descryption = new AP4_SampleDescription(AP4_SampleDescription::TYPE_UNKNOWN, 0, 0);

    if (repr->GetPsshSetPos() != PSSHSET_POS_DEFAULT)
    {
      AP4_ContainerAtom schi{AP4_ATOM_TYPE_SCHI};
      schi.AddChild(
          new AP4_TencAtom(AP4_CENC_CIPHER_AES_128_CTR, 8,
                           GetDefaultKeyId(repr->GetPsshSetPos())));
      sample_descryption = new AP4_ProtectedSampleDescription(
          0, sample_descryption, 0, AP4_PROTECTION_SCHEME_TYPE_PIFF, 0, "", &schi);
    }
    sample_table->AddSampleDescription(sample_descryption);
    movie->AddTrack(new AP4_Track(static_cast<AP4_Track::Type>(stream->m_adStream.GetTrackType()),
                                  sample_table, CFragmentedSampleReader::TRACKID_UNKNOWN,
                                  repr->GetTimescale(), 0, repr->GetTimescale(), 0, "", 0, 0));
    //Create a dumy MOOV Atom to tell Bento4 its a fragmented stream
    AP4_MoovAtom* moov{new AP4_MoovAtom()};
    moov->AddChild(new AP4_ContainerAtom(AP4_ATOM_TYPE_MVEX));
    movie->SetMoovAtom(moov);
    return movie;
  }
  return nullptr;
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
  bool bReset = true;
  ISampleReader* streamReader = stream->GetReader();
  if (timing)
  {
    seekTime += stream->m_adStream.GetAbsolutePTSOffset();
  }
  else
  {
    seekTime -= ptsDiff;
    streamReader->SetStartPTS(GetTimingStartPTS());
  }

  stream->m_adStream.seek_time(static_cast<double>(seekTime / STREAM_TIME_BASE), preceeding,
                               bReset);

  if (!streamReader)
  {
    LOG::LogF(LOGERROR, "Cannot get the stream reader");
    return;
  }

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
  CStream* timingStream{GetTimingStream()};

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
      else if (!streamReader->EOS())
      {
        // Once the start PTS has been acquired for the timing stream, set this value
        // to the other stream readers
        if (stream.get() != timingStream &&
            timingStream->GetReader()->GetStartPTS() != STREAM_NOPTS_VALUE &&
            streamReader->GetStartPTS() == STREAM_NOPTS_VALUE)
        {
          // want this to be the internal data's (not segment's) pts of
          // the first segment in period
          streamReader->SetStartPTS(GetTimingStartPTS());
        }
        if (AP4_SUCCEEDED(streamReader->Start(isStarted)))
        {
          if (!res || streamReader->DTSorPTS() < res->GetReader()->DTSorPTS())
          {
            if (stream->m_adStream.waitingForSegment(true))
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
    CheckFragmentDuration(*res);
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
  if (m_adaptiveTree->has_timeshift_buffer_)
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

      stream->m_hasSegmentChanged = true;
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

void CSession::CheckFragmentDuration(CStream& stream)
{
  uint64_t nextTs;
  uint64_t nextDur;
  ISampleReader* streamReader{stream.GetReader()};
  if (!streamReader)
  {
    LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
    return;
  }

  if (stream.m_hasSegmentChanged && streamReader->GetNextFragmentInfo(nextTs, nextDur))
  {
    m_adaptiveTree->SetFragmentDuration(
        stream.m_adStream.getPeriod(), stream.m_adStream.getAdaptationSet(),
        stream.m_adStream.getRepresentation(), stream.m_adStream.getSegmentPos(), nextTs,
        static_cast<uint32_t>(nextDur), streamReader->GetTimeScale());
  }
  stream.m_hasSegmentChanged = false;
}

const AP4_UI08* CSession::GetDefaultKeyId(const uint16_t index) const
{
  static const AP4_UI08 default_key[16]{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  if (m_adaptiveTree->m_currentPeriod->GetPSSHSets()[index].defaultKID_.size() == 16)
    return reinterpret_cast<const AP4_UI08*>(
        m_adaptiveTree->m_currentPeriod->GetPSSHSets()[index].defaultKID_.data());

  return default_key;
}

Adaptive_CencSingleSampleDecrypter* CSession::GetSingleSampleDecrypter(std::string sessionId)
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

STREAM_CRYPTO_KEY_SYSTEM CSession::GetCryptoKeySystem() const
{
  if (m_kodiProps.m_licenseType == "com.widevine.alpha")
    return STREAM_CRYPTO_KEY_SYSTEM_WIDEVINE;
#if STREAMCRYPTO_VERSION_LEVEL >= 1
  else if (m_kodiProps.m_licenseType == "com.huawei.wiseplay")
    return STREAM_CRYPTO_KEY_SYSTEM_WISEPLAY;
#endif
  else if (m_kodiProps.m_licenseType == "com.microsoft.playready")
    return STREAM_CRYPTO_KEY_SYSTEM_PLAYREADY;
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

uint64_t CSession::GetTimingStartPTS() const
{
  if (CStream* timing = GetTimingStream())
  {
    return timing->GetReader()->GetStartPTS();
  }
  else
    return 0;
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
  if (m_adaptiveTree->m_nextPeriod)
    return true;

  --ch;
  if (ch >= 0 && ch < static_cast<int>(m_adaptiveTree->m_periods.size()) &&
      m_adaptiveTree->m_periods[ch].get() != m_adaptiveTree->m_currentPeriod)
  {
    CPeriod* nextPeriod = m_adaptiveTree->m_periods[ch].get();
    m_adaptiveTree->m_nextPeriod = nextPeriod;
    LOG::LogF(LOGDEBUG, "Switching to new Period (id=%s, start=%ld, seq=%d)",
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
