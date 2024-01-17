/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Session.h"

#include "aes_decrypter.h"
#include "parser/DASHTree.h"
#include "parser/HLSTree.h"
#include "parser/SmoothTree.h"
#include "samplereader/ADTSSampleReader.h"
#include "samplereader/FragmentedSampleReader.h"
#include "samplereader/SubtitleSampleReader.h"
#include "samplereader/TSSampleReader.h"
#include "samplereader/WebmSampleReader.h"
#include "utils/Base64Utils.h"
#include "utils/SettingsUtils.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <kodi/addon-instance/Inputstream.h>

using namespace UTILS;
using namespace kodi::tools;
using namespace SESSION;

static const AP4_Track::Type TIDC[adaptive::AdaptiveTree::STREAM_TYPE_COUNT] = {
    AP4_Track::TYPE_UNKNOWN, AP4_Track::TYPE_VIDEO, AP4_Track::TYPE_AUDIO,
    AP4_Track::TYPE_SUBTITLES};


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

  switch (kodiProps.m_manifestType)
  {
    case PROPERTIES::ManifestType::MPD:
      m_adaptiveTree = new adaptive::DASHTree(m_reprChooser);
      break;
    case PROPERTIES::ManifestType::ISM:
      m_adaptiveTree = new adaptive::SmoothTree(m_reprChooser);
      break;
    case PROPERTIES::ManifestType::HLS:
      m_adaptiveTree = new adaptive::HLSTree(m_reprChooser);
      break;
    default:
      LOG::LogF(LOGFATAL, "Manifest type not handled");
      return;
  };

  m_adaptiveTree->Configure(kodiProps);

  m_settingNoSecureDecoder = kodi::addon::GetSettingBoolean("NOSECUREDECODER");
  LOG::Log(LOGDEBUG, "Setting NOSECUREDECODER value: %d", m_settingNoSecureDecoder);

  m_settingIsHdcpOverride = kodi::addon::GetSettingBoolean("HDCPOVERRIDE");
  LOG::Log(LOGDEBUG, "Ignore HDCP status setting value: %i", m_settingIsHdcpOverride);

  switch (kodi::addon::GetSettingInt("MEDIATYPE"))
  {
    case 1:
      m_mediaTypeMask = static_cast<uint8_t>(1U) << adaptive::AdaptiveTree::AUDIO;
      break;
    case 2:
      m_mediaTypeMask = static_cast<uint8_t>(1U) << adaptive::AdaptiveTree::VIDEO;
      break;
    case 3:
      m_mediaTypeMask = (static_cast<uint8_t>(1U) << adaptive::AdaptiveTree::VIDEO) |
                        (static_cast<uint8_t>(1U) << adaptive::AdaptiveTree::SUBTITLE);
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

  m_adaptiveTree->Uninitialize();

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

  std::vector<std::string> searchPaths{2};
  searchPaths[0] =
      kodi::vfs::TranslateSpecialProtocol("special://xbmcbinaddons/inputstream.adaptive/");
  searchPaths[1] = kodi::addon::GetAddonInfo("path");

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
  if (!m_adaptiveTree)
    return false;

  // Get URN's wich are supported by this addon
  if (!m_kodiProps.m_licenseType.empty())
  {
    SetSupportedDecrypterURN(m_adaptiveTree->m_supportedKeySystem);
    LOG::Log(LOGDEBUG, "Supported URN: %s", m_adaptiveTree->m_supportedKeySystem.c_str());
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

  // Open manifest file with location redirect support  bool mpdSuccess;
  std::string manifestUrl =
      m_adaptiveTree->location_.empty() ? m_manifestUrl : m_adaptiveTree->location_;

  m_adaptiveTree->SetManifestUpdateParam(manifestUrl, m_kodiProps.m_manifestUpdateParam);

  if (!m_adaptiveTree->open(manifestUrl, addHeaders) || m_adaptiveTree->empty())
  {
    LOG::Log(LOGERROR, "Could not open / parse manifest (%s)", manifestUrl.c_str());
    return false;
  }
  LOG::Log(
      LOGINFO,
      "Successfully parsed manifest file (Periods: %ld, Streams in first period: %ld, Type: %s)",
      m_adaptiveTree->periods_.size(), m_adaptiveTree->current_period_->adaptationSets_.size(),
      m_adaptiveTree->has_timeshift_buffer_ ? "live" : "VOD");

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
  adaptive::AdaptiveTree::AdaptationSet* adp{nullptr};

  while ((adp = m_adaptiveTree->GetAdaptationSet(adpIndex++)))
  {
    if (adp->type_ != adaptive::AdaptiveTree::StreamType::VIDEO)
      continue;

    for (auto it = adp->representations_.begin(); it != adp->representations_.end();)
    {
      const adaptive::AdaptiveTree::Representation* repr = *it;
      const SSD::SSD_DECRYPTER::SSD_CAPS& ssd_caps = decrypterCaps[repr->pssh_set_];

      if (repr->hdcpVersion_ > ssd_caps.hdcpVersion ||
          (ssd_caps.hdcpLimit > 0 && repr->width_ * repr->height_ > ssd_caps.hdcpLimit))
      {
        LOG::Log(LOGDEBUG, "Representation ID \"%s\" removed as not HDCP compliant",
                 repr->id.c_str());
        delete repr;
        it = adp->representations_.erase(it);
      }
      else
        it++;
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
  m_cdmSessions.resize(m_adaptiveTree->current_period_->psshSets_.size());
  memset(&m_cdmSessions.front(), 0, sizeof(CCdmSession));

  // Try to initialize an SingleSampleDecryptor
  if (m_adaptiveTree->current_period_->encryptionState_)
  {
    std::string licenseKey{m_kodiProps.m_licenseKey};

    if (licenseKey.empty())
      licenseKey = m_adaptiveTree->license_url_;

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

      auto sessionPsshset{m_adaptiveTree->current_period_->psshSets_[ses]};

      if (sessionPsshset.pssh_ == "FILE")
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
                else if (AP4_Track* track = movie->GetTrack(TIDC[stream.m_adStream.get_type()]))
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
          if (sessionPsshset.pssh_ == m_adaptiveTree->current_period_->psshSets_[i].pssh_)
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
          m_adaptiveTree->current_period_->RemovePSSHSet(static_cast<std::uint16_t>(ses));
        }
        else if (session.m_decrypterCaps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH)
        {
          session.m_cdmSessionStr = session.m_cencSingleSampleDecrypter->GetSessionId();
          isSecureVideoSession = true;

          if (m_settingNoSecureDecoder && !m_kodiProps.m_isLicenseForceSecureDecoder &&
              !m_adaptiveTree->current_period_->need_secure_decoder_)
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
  if (m_adaptiveTree->next_period_)
  {
    psshChanged =
        !(m_adaptiveTree->current_period_->psshSets_ == m_adaptiveTree->next_period_->psshSets_);
    m_adaptiveTree->current_period_ = m_adaptiveTree->next_period_;
    m_adaptiveTree->next_period_ = nullptr;
  }

  m_chapterStartTime = GetChapterStartTime();

  if (m_adaptiveTree->current_period_->encryptionState_ ==
      adaptive::AdaptiveTree::ENCRYTIONSTATE_ENCRYPTED)
  {
    LOG::Log(LOGERROR, "Unable to handle decryption. Unsupported!");
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
  adaptive::AdaptiveTree::AdaptationSet* adp{nullptr};
  SETTINGS::StreamSelection streamSelectionMode{m_reprChooser->GetStreamSelectionMode()};

  while ((adp = m_adaptiveTree->GetAdaptationSet(adpIndex++)))
  {
    if (adp->representations_.empty())
      continue;

    bool isManualStreamSelection;
    if (adp->type_ == adaptive::AdaptiveTree::StreamType::VIDEO)
      isManualStreamSelection = streamSelectionMode != SETTINGS::StreamSelection::AUTO;
    else
      isManualStreamSelection = streamSelectionMode == SETTINGS::StreamSelection::MANUAL;

    // Get the default initial stream repr. based on "adaptive repr. chooser"
    auto defaultRepr{m_reprChooser->GetRepresentation(adp)};

    if (isManualStreamSelection)
    {
      // Add all stream representations
      for (size_t i{0}; i < adp->representations_.size(); i++)
      {
        size_t reprIndex{adp->representations_.size() - i};
        uint32_t uniqueId{adpIndex};
        uniqueId |= reprIndex << 16;

        adaptive::AdaptiveTree::Representation* currentRepr{adp->representations_[i]};
        bool isDefaultRepr{currentRepr == defaultRepr};

        AddStream(adp, currentRepr, isDefaultRepr, uniqueId);
      }
    }
    else
    {
      // Add the default stream representation only
      size_t reprIndex{adp->representations_.size()};
      uint32_t uniqueId{adpIndex};
      uniqueId |= reprIndex << 16;

      AddStream(adp, defaultRepr, true, uniqueId);
    }
  }

  m_firstPeriodInitialized = true;
  return true;
}

void CSession::AddStream(adaptive::AdaptiveTree::AdaptationSet* adp,
                         adaptive::AdaptiveTree::Representation* initialRepr,
                         bool isDefaultRepr,
                         uint32_t uniqueId)
{
  m_streams.push_back(std::make_unique<CStream>(*m_adaptiveTree, adp, initialRepr, m_kodiProps,
                                                m_firstPeriodInitialized));

  CStream& stream{*m_streams.back()};

  uint32_t flags{INPUTSTREAM_FLAG_NONE};
  size_t copySize{adp->name_.size() > 255 ? 255 : adp->name_.size()};
  stream.m_info.SetName(adp->name_);

  switch (adp->type_)
  {
    case adaptive::AdaptiveTree::VIDEO:
    {
      stream.m_info.SetStreamType(INPUTSTREAM_TYPE_VIDEO);
      if (isDefaultRepr)
        flags |= INPUTSTREAM_FLAG_DEFAULT;
      break;
    }
    case adaptive::AdaptiveTree::AUDIO:
    {
      stream.m_info.SetStreamType(INPUTSTREAM_TYPE_AUDIO);
      if (adp->impaired_)
        flags |= INPUTSTREAM_FLAG_VISUAL_IMPAIRED;
      if (adp->default_)
        flags |= INPUTSTREAM_FLAG_DEFAULT;
      if (adp->original_ || (!m_kodiProps.m_audioLanguageOrig.empty() &&
                             adp->language_ == m_kodiProps.m_audioLanguageOrig))
      {
        flags |= INPUTSTREAM_FLAG_ORIGINAL;
      }
      break;
    }
    case adaptive::AdaptiveTree::SUBTITLE:
    {
      stream.m_info.SetStreamType(INPUTSTREAM_TYPE_SUBTITLE);
      if (adp->impaired_)
        flags |= INPUTSTREAM_FLAG_HEARING_IMPAIRED;
      if (adp->forced_)
        flags |= INPUTSTREAM_FLAG_FORCED;
      if (adp->default_)
        flags |= INPUTSTREAM_FLAG_DEFAULT;
      break;
    }
    default:
      break;
  }

  stream.m_info.SetFlags(flags);
  stream.m_info.SetPhysicalIndex(uniqueId);
  stream.m_info.SetLanguage(adp->language_);
  stream.m_info.ClearExtraData();
  stream.m_info.SetFeatures(0);

  stream.m_adStream.set_observer(dynamic_cast<adaptive::AdaptiveStreamObserver*>(this));

  UpdateStream(stream);
}

void CSession::UpdateStream(CStream& stream)
{
  const adaptive::AdaptiveTree::Representation* rep{stream.m_adStream.getRepresentation()};
  const SSD::SSD_DECRYPTER::SSD_CAPS& caps{GetDecrypterCaps(rep->pssh_set_)};

  stream.m_info.SetWidth(static_cast<uint32_t>(rep->width_));
  stream.m_info.SetHeight(static_cast<uint32_t>(rep->height_));
  stream.m_info.SetAspect(rep->aspect_);

  if (stream.m_info.GetAspect() == 0.0f && stream.m_info.GetHeight())
    stream.m_info.SetAspect((float)stream.m_info.GetWidth() / stream.m_info.GetHeight());
  stream.m_isEncrypted = rep->get_psshset() > 0;

  stream.m_info.SetExtraData(nullptr, 0);
  if (rep->codec_private_data_.size())
  {
    std::string annexb;
    const std::string* res(&annexb);

    if ((caps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_ANNEXB_REQUIRED) &&
        stream.m_info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
    {
      LOG::Log(LOGDEBUG, "UpdateStream: Convert avc -> annexb");
      annexb = AvcToAnnexb(rep->codec_private_data_);
    }
    else
    {
      res = &rep->codec_private_data_;
    }
    stream.m_info.SetExtraData(reinterpret_cast<const uint8_t*>(res->data()), res->size());
  }

  // we currently use only the first track!
  size_t pos{rep->codecs_.find(",")};
  if (pos == std::string::npos)
    pos = rep->codecs_.size();

  stream.m_info.SetCodecInternalName(rep->codecs_);
  stream.m_info.SetCodecFourCC(0);

#if INPUTSTREAM_VERSION_LEVEL > 0
  stream.m_info.SetColorSpace(INPUTSTREAM_COLORSPACE_UNSPECIFIED);
  stream.m_info.SetColorRange(INPUTSTREAM_COLORRANGE_UNKNOWN);
  stream.m_info.SetColorPrimaries(INPUTSTREAM_COLORPRIMARY_UNSPECIFIED);
  stream.m_info.SetColorTransferCharacteristic(INPUTSTREAM_COLORTRC_UNSPECIFIED);
#else
  stream.m_info.SetColorSpace(INPUTSTREAM_COLORSPACE_UNKNOWN);
  stream.m_info.SetColorRange(INPUTSTREAM_COLORRANGE_UNKNOWN);
#endif
  if (rep->codecs_.find("mp4a") == 0 || rep->codecs_.find("aac") == 0)
    stream.m_info.SetCodecName("aac");
  else if (rep->codecs_.find("dts") == 0)
    stream.m_info.SetCodecName("dca");
  else if (rep->codecs_.find("ac-3") == 0)
    stream.m_info.SetCodecName("ac3");
  else if (rep->codecs_.find("ec-3") == 0)
    stream.m_info.SetCodecName("eac3");
  else if (rep->codecs_.find("avc") == 0 || rep->codecs_.find("h264") == 0)
    stream.m_info.SetCodecName("h264");
  else if (rep->codecs_.find("hev") == 0)
    stream.m_info.SetCodecName("hevc");
  else if (rep->codecs_.find("hvc") == 0 || rep->codecs_.find("dvh") == 0)
  {
    stream.m_info.SetCodecFourCC(
        MakeFourCC(rep->codecs_[0], rep->codecs_[1], rep->codecs_[2], rep->codecs_[3]));
    stream.m_info.SetCodecName("hevc");
  }
  else if (rep->codecs_.find("vp9") == 0 || rep->codecs_.find("vp09") == 0)
  {
    stream.m_info.SetCodecName("vp9");
#if INPUTSTREAM_VERSION_LEVEL > 0
    if ((pos = rep->codecs_.find(".")) != std::string::npos)
      stream.m_info.SetCodecProfile(static_cast<STREAMCODEC_PROFILE>(
          VP9CodecProfile0 + atoi(rep->codecs_.c_str() + (pos + 1))));
#endif
  }
  else if (rep->codecs_.find("av1") == 0 || rep->codecs_.find("av01") == 0)
    stream.m_info.SetCodecName("av1");
  else if (rep->codecs_.find("opus") == 0)
    stream.m_info.SetCodecName("opus");
  else if (rep->codecs_.find("vorbis") == 0)
    stream.m_info.SetCodecName("vorbis");
  else if (rep->codecs_.find("stpp") == 0 || rep->codecs_.find("ttml") == 0)
    stream.m_info.SetCodecName("srt");
  else if (rep->codecs_.find("wvtt") == 0)
    stream.m_info.SetCodecName("webvtt");
  else
  {
    LOG::Log(LOGWARNING, "Unsupported codec %s in stream ID: %s", rep->codecs_.c_str(),
             rep->id.c_str());
    stream.m_isValid = false;
  }

  // We support currently only mp4 / ts / adts
  if (rep->containerType_ != adaptive::AdaptiveTree::CONTAINERTYPE_NOTYPE &&
      rep->containerType_ != adaptive::AdaptiveTree::CONTAINERTYPE_MP4 &&
      rep->containerType_ != adaptive::AdaptiveTree::CONTAINERTYPE_TS &&
      rep->containerType_ != adaptive::AdaptiveTree::CONTAINERTYPE_ADTS &&
      rep->containerType_ != adaptive::AdaptiveTree::CONTAINERTYPE_WEBM &&
      rep->containerType_ != adaptive::AdaptiveTree::CONTAINERTYPE_TEXT)
  {
    LOG::Log(LOGWARNING, "Unsupported container in stream ID: %s", rep->id.c_str());
    stream.m_isValid = false;
  }

  stream.m_info.SetFpsRate(rep->fpsRate_);
  stream.m_info.SetFpsScale(rep->fpsScale_);
  stream.m_info.SetSampleRate(rep->samplingRate_);
  stream.m_info.SetChannels(rep->channelCount_);
  stream.m_info.SetBitRate(rep->bandwidth_);
}

AP4_Movie* CSession::PrepareStream(CStream* stream, bool& needRefetch)
{
  needRefetch = false;
  switch (m_adaptiveTree->prepareRepresentation(stream->m_adStream.getPeriod(),
                                                stream->m_adStream.getAdaptationSet(),
                                                stream->m_adStream.getRepresentation()))
  {
    case adaptive::AdaptiveTree::PREPARE_RESULT_FAILURE:
      return nullptr;
    case adaptive::AdaptiveTree::PREPARE_RESULT_DRMCHANGED:
      if (!InitializeDRM())
        return nullptr;
    case adaptive::AdaptiveTree::PREPARE_RESULT_DRMUNCHANGED:
      stream->m_isEncrypted = stream->m_adStream.getRepresentation()->pssh_set_ > 0;
      needRefetch = true;
      break;
    default:;
  }

  if (stream->m_adStream.getRepresentation()->containerType_ ==
          adaptive::AdaptiveTree::CONTAINERTYPE_MP4 &&
      (stream->m_adStream.getRepresentation()->flags_ &
       adaptive::AdaptiveTree::Representation::INITIALIZATION_PREFIXED) == 0 &&
      stream->m_adStream.getRepresentation()->get_initialization() == nullptr)
  {
    //We'll create a Movie out of the things we got from manifest file
    //note: movie will be deleted in destructor of stream->input_file_
    AP4_Movie* movie{new AP4_Movie()};

    AP4_SyntheticSampleTable* sample_table{new AP4_SyntheticSampleTable()};

    AP4_SampleDescription* sample_descryption;
    if (stream->m_info.GetCodecName() == "h264")
    {
      const std::string& extradata{stream->m_adStream.getRepresentation()->codec_private_data_};
      AP4_MemoryByteStream ms{reinterpret_cast<const uint8_t*>(extradata.data()),
                              static_cast<const AP4_Size>(extradata.size())};
      AP4_AvccAtom* atom{AP4_AvccAtom::Create(AP4_ATOM_HEADER_SIZE + extradata.size(), ms)};
      sample_descryption =
          new AP4_AvcSampleDescription(AP4_SAMPLE_FORMAT_AVC1, stream->m_info.GetWidth(),
                                       stream->m_info.GetHeight(), 0, nullptr, atom);
    }
    else if (stream->m_info.GetCodecName() == "hevc")
    {
      const std::string& extradata{(stream->m_adStream.getRepresentation()->codec_private_data_)};
      AP4_MemoryByteStream ms{reinterpret_cast<const AP4_UI08*>(extradata.data()),
                              static_cast<const AP4_Size>(extradata.size())};
      AP4_HvccAtom* atom{AP4_HvccAtom::Create(AP4_ATOM_HEADER_SIZE + extradata.size(), ms)};
      sample_descryption =
          new AP4_HevcSampleDescription(AP4_SAMPLE_FORMAT_HEV1, stream->m_info.GetWidth(),
                                        stream->m_info.GetHeight(), 0, nullptr, atom);
    }
    else if (stream->m_info.GetCodecName() == "av1")
    {
      const std::string& extradata(stream->m_adStream.getRepresentation()->codec_private_data_);
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

    if (stream->m_adStream.getRepresentation()->get_psshset() > 0)
    {
      AP4_ContainerAtom schi{AP4_ATOM_TYPE_SCHI};
      schi.AddChild(
          new AP4_TencAtom(AP4_CENC_CIPHER_AES_128_CTR, 8,
                           GetDefaultKeyId(stream->m_adStream.getRepresentation()->get_psshset())));
      sample_descryption = new AP4_ProtectedSampleDescription(
          0, sample_descryption, 0, AP4_PROTECTION_SCHEME_TYPE_PIFF, 0, "", &schi);
    }
    sample_table->AddSampleDescription(sample_descryption);

    movie->AddTrack(new AP4_Track(TIDC[stream->m_adStream.get_type()], sample_table,
                                  CFragmentedSampleReader::TRACKID_UNKNOWN,
                                  stream->m_adStream.getRepresentation()->timescale_, 0,
                                  stream->m_adStream.getRepresentation()->timescale_, 0, "", 0, 0));
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
      else if (!streamReader->EOS())
      {
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
  std::vector<adaptive::AdaptiveTree::Period*>::const_iterator pi;
  for (pi = m_adaptiveTree->periods_.cbegin(); pi != m_adaptiveTree->periods_.cend(); ++pi)
  {
    chapterTime += double((*pi)->duration_) / (*pi)->timescale_;
    if (chapterTime > seekTime)
      break;
  }

  if (pi == m_adaptiveTree->periods_.end())
    --pi;
  chapterTime -= double((*pi)->duration_) / (*pi)->timescale_;

  if ((*pi) != m_adaptiveTree->current_period_)
  {
    LOG::Log(LOGDEBUG, "SeekTime: seeking into new chapter: %d",
             static_cast<int>((pi - m_adaptiveTree->periods_.begin()) + 1));
    SeekChapter((pi - m_adaptiveTree->periods_.begin()) + 1);
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
        stream.m_adStream.getAdaptationSet(), stream.m_adStream.getRepresentation(),
        stream.m_adStream.getSegmentPos(), nextTs, static_cast<uint32_t>(nextDur),
        streamReader->GetTimeScale());
  }
  stream.m_hasSegmentChanged = false;
}

const AP4_UI08* CSession::GetDefaultKeyId(const uint16_t index) const
{
  static const AP4_UI08 default_key[16]{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  if (m_adaptiveTree->current_period_->psshSets_[index].defaultKID_.size() == 16)
    return reinterpret_cast<const AP4_UI08*>(
        m_adaptiveTree->current_period_->psshSets_[index].defaultKID_.data());

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
  const INPUTSTREAM_TYPE adp2ips[] = {INPUTSTREAM_TYPE_NONE, INPUTSTREAM_TYPE_VIDEO,
                                      INPUTSTREAM_TYPE_AUDIO, INPUTSTREAM_TYPE_SUBTITLE};
  uint32_t res{0};
  for (unsigned int i(0); i < 4; ++i)
  {
    if (m_adaptiveTree->current_period_->included_types_ & (1U << i))
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
    std::vector<adaptive::AdaptiveTree::Period*>::const_iterator res =
        std::find(m_adaptiveTree->periods_.cbegin(), m_adaptiveTree->periods_.cend(),
                  m_adaptiveTree->current_period_);
    if (res != m_adaptiveTree->periods_.cend())
      return (res - m_adaptiveTree->periods_.cbegin()) + 1;
  }
  return -1;
}

int CSession::GetChapterCount() const
{
  if (m_adaptiveTree)
    return m_adaptiveTree->periods_.size() > 1 ? m_adaptiveTree->periods_.size() : 0;

  return 0;
}

const char* CSession::GetChapterName(int ch) const
{
  --ch;
  if (ch >= 0 && ch < static_cast<int>(m_adaptiveTree->periods_.size()))
    return m_adaptiveTree->periods_[ch]->id_.c_str();

  return "[Unknown]";
}

int64_t CSession::GetChapterPos(int ch) const
{
  int64_t sum{0};
  --ch;

  for (; ch; --ch)
  {
    sum += (m_adaptiveTree->periods_[ch - 1]->duration_ * STREAM_TIME_BASE) /
           m_adaptiveTree->periods_[ch - 1]->timescale_;
  }

  return sum / STREAM_TIME_BASE;
}

uint64_t CSession::GetChapterStartTime() const
{
  uint64_t start_time = 0;
  for (adaptive::AdaptiveTree::Period* p : m_adaptiveTree->periods_)
  {
    if (p == m_adaptiveTree->current_period_)
      break;
    else
      start_time += (p->duration_ * STREAM_TIME_BASE) / p->timescale_;
  }
  return start_time;
}

int CSession::GetPeriodId() const
{
  if (m_adaptiveTree)
  {
    if (IsLive())
    {
      return m_adaptiveTree->current_period_->sequence_ == m_adaptiveTree->initial_sequence_
                 ? 1
                 : m_adaptiveTree->current_period_->sequence_ + 1;
    }
    else
      return GetChapter();
  }
  return -1;
}

bool CSession::SeekChapter(int ch)
{
  if (m_adaptiveTree->next_period_)
    return true;

  --ch;
  if (ch >= 0 && ch < static_cast<int>(m_adaptiveTree->periods_.size()) &&
      m_adaptiveTree->periods_[ch] != m_adaptiveTree->current_period_)
  {
    auto newPd = m_adaptiveTree->periods_[ch];
    m_adaptiveTree->next_period_ = newPd;
    LOG::LogF(LOGDEBUG, "Switching to new Period (id=%s, start=%ld, seq=%d)", newPd->id_.c_str(),
              newPd->start_, newPd->sequence_);
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
