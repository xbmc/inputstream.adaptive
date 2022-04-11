/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "main.h"

#include "ADTSReader.h"
#include "TSReader.h"
#include "WebmReader.h"
#include "aes_decrypter.h"
#include "oscompat.h"
#include "codechandler/AVCCodecHandler.h"
#include "codechandler/HEVCCodecHandler.h"
#include "codechandler/MPEGCodecHandler.h"
#include "codechandler/TTMLCodecHandler.h"
#include "codechandler/VP9CodecHandler.h"
#include "codechandler/WebVTTCodecHandler.h"
#include "common/RepresentationChooserDefault.h"
#include "samplereader/ADTSSampleReader.h"
#include "samplereader/DummySampleReader.h"
#include "samplereader/FragmentedSampleReader.h"
#include "samplereader/SubtitleSampleReader.h"
#include "samplereader/TSSampleReader.h"
#include "samplereader/WebmSampleReader.h"
#include "parser/DASHTree.h"
#include "parser/HLSTree.h"
#include "parser/SmoothTree.h"
#include "utils/Base64Utils.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"

#include <chrono>
#include <algorithm>
#include <iostream>
#include <math.h>
#include <sstream>
#include <stdarg.h> // va_list, va_start, va_arg, va_end
#include <stdio.h>
#include <string.h>

#include <kodi/Filesystem.h>
#include <kodi/General.h>
#include <kodi/addon-instance/VideoCodec.h>
#include <kodi/addon-instance/inputstream/StreamCodec.h>
#include "kodi/tools/StringUtils.h"

#if defined(ANDROID)
#include <kodi/platform/android/System.h>
#endif

#ifdef CreateDirectory
#undef CreateDirectory
#endif

#define STREAM_TIME_BASE 1000000

using namespace UTILS;
using namespace kodi::tools;

static const AP4_Track::Type TIDC[adaptive::AdaptiveTree::STREAM_TYPE_COUNT] = {
    AP4_Track::TYPE_UNKNOWN, AP4_Track::TYPE_VIDEO, AP4_Track::TYPE_AUDIO,
    AP4_Track::TYPE_SUBTITLES};

/*******************************************************
kodi host - interface for decrypter libraries
********************************************************/
class ATTR_DLL_LOCAL KodiHost : public SSD::SSD_HOST
{
public:
#if defined(ANDROID)
  virtual void* GetJNIEnv() override { return m_androidSystem.GetJNIEnv(); };

  virtual int GetSDKVersion() override { return m_androidSystem.GetSDKVersion(); };

  virtual const char* GetClassName() override
  {
    m_retvalHelper = m_androidSystem.GetClassName();
    return m_retvalHelper.c_str();
  };

#endif
  virtual const char* GetLibraryPath() const override { return m_strLibraryPath.c_str(); };

  virtual const char* GetProfilePath() const override { return m_strProfilePath.c_str(); };

  virtual void* CURLCreate(const char* strURL) override
  {
    kodi::vfs::CFile* file = new kodi::vfs::CFile;
    if (!file->CURLCreate(strURL))
    {
      delete file;
      return nullptr;
    }
    return file;
  };

  virtual bool CURLAddOption(void* file,
                             CURLOPTIONS opt,
                             const char* name,
                             const char* value) override
  {
    const CURLOptiontype xbmcmap[] = {ADDON_CURL_OPTION_PROTOCOL, ADDON_CURL_OPTION_HEADER};
    return static_cast<kodi::vfs::CFile*>(file)->CURLAddOption(xbmcmap[opt], name, value);
  }

  virtual const char* CURLGetProperty(void* file, CURLPROPERTY prop, const char* name) override
  {
    const FilePropertyTypes xbmcmap[] = {ADDON_FILE_PROPERTY_RESPONSE_HEADER};
    m_strPropertyValue =
        static_cast<kodi::vfs::CFile*>(file)->GetPropertyValue(xbmcmap[prop], name);
    return m_strPropertyValue.c_str();
  }

  virtual bool CURLOpen(void* file) override
  {
    return static_cast<kodi::vfs::CFile*>(file)->CURLOpen(ADDON_READ_NO_CACHE);
  };

  virtual size_t ReadFile(void* file, void* lpBuf, size_t uiBufSize) override
  {
    return static_cast<kodi::vfs::CFile*>(file)->Read(lpBuf, uiBufSize);
  };

  virtual void CloseFile(void* file) override
  {
    return static_cast<kodi::vfs::CFile*>(file)->Close();
  };

  virtual bool CreateDir(const char* dir) override { return kodi::vfs::CreateDirectory(dir); };

  void LogVA(const SSD::SSDLogLevel level, const char* format, va_list args) override
  {
    std::vector<char> data;
    data.resize(256);

    va_list argsStart;
    va_copy(argsStart, args);

    int ret;
    while ((ret = vsnprintf(data.data(), data.size(), format, args)) > data.size())
    {
      data.resize(data.size() * 2);
      args = argsStart;
    }
    LOG::Log(static_cast<LogLevel>(level), data.data());
    va_end(argsStart);
  };

  void SetLibraryPath(const char* libraryPath)
  {
    m_strLibraryPath = libraryPath;

    const char* pathSep(libraryPath[0] && libraryPath[1] == ':' && isalpha(libraryPath[0]) ? "\\"
                                                                                           : "/");

    if (m_strLibraryPath.size() && m_strLibraryPath.back() != pathSep[0])
      m_strLibraryPath += pathSep;
  }

  void SetProfilePath(const std::string& profilePath)
  {
    m_strProfilePath = profilePath;

    const char* pathSep(profilePath[0] && profilePath[1] == ':' && isalpha(profilePath[0]) ? "\\"
                                                                                           : "/");

    if (m_strProfilePath.size() && m_strProfilePath.back() != pathSep[0])
      m_strProfilePath += pathSep;

    //let us make cdm userdata out of the addonpath and share them between addons
    m_strProfilePath.resize(
        m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 2));
    m_strProfilePath.resize(
        m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 1));
    m_strProfilePath.resize(
        m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 1) + 1);

    kodi::vfs::CreateDirectory(m_strProfilePath.c_str());
    m_strProfilePath += "cdm";
    m_strProfilePath += pathSep;
    kodi::vfs::CreateDirectory(m_strProfilePath.c_str());
  }

  virtual bool GetBuffer(void* instance, SSD::SSD_PICTURE& picture) override
  {
    return instance ? static_cast<kodi::addon::CInstanceVideoCodec*>(instance)->GetFrameBuffer(
                          *reinterpret_cast<VIDEOCODEC_PICTURE*>(&picture))
                    : false;
  }

  virtual void ReleaseBuffer(void* instance, void* buffer) override
  {
    if (instance)
      static_cast<kodi::addon::CInstanceVideoCodec*>(instance)->ReleaseFrameBuffer(buffer);
  }

private:
  std::string m_strProfilePath, m_strLibraryPath, m_strPropertyValue;

#if defined(ANDROID)
  kodi::platform::CInterfaceAndroidSystem m_androidSystem;
  std::string m_retvalHelper;
#endif
} * kodihost;

/*******************************************************
Kodi Streams implementation
********************************************************/

bool adaptive::AdaptiveTree::download(const std::string& url,
                                      const std::map<std::string, std::string>& manifestHeaders,
                                      void* opaque,
                                      bool isManifest)
{
  // open the file
  kodi::vfs::CFile file;
  if (!file.CURLCreate(url))
    return false;

  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "seekable", "0");
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "acceptencoding", "gzip");

  for (const auto& entry : manifestHeaders)
  {
    file.CURLAddOption(ADDON_CURL_OPTION_HEADER, entry.first.c_str(), entry.second.c_str());
  }

  if (!file.CURLOpen(ADDON_READ_CHUNKED | ADDON_READ_NO_CACHE))
  {
    LOG::Log(LOGERROR, "Download failed: %s", url.c_str());
    return false;
  }

  effective_url_ = file.GetPropertyValue(ADDON_FILE_PROPERTY_EFFECTIVE_URL, "");

  if (isManifest && !PreparePaths(effective_url_))
  {
    file.Close();
    return false;
  }

  // read the file
  static const unsigned int CHUNKSIZE = 16384;
  char buf[CHUNKSIZE];
  size_t nbRead;

  while ((nbRead = file.Read(buf, CHUNKSIZE)) > 0 && ~nbRead && write_data(buf, nbRead, opaque))
    ;

  etag_ = file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, "etag");
  last_modified_ = file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, "last-modified");

  //m_downloadCurrentSpeed = file.GetFileDownloadSpeed();

  file.Close();

  LOG::Log(LOGDEBUG, "Download finished: %s", effective_url_.c_str());

  return nbRead == 0;
}

/*******************************************************
Main class Session
********************************************************/

void Session::STREAM::disable()
{
  if (enabled)
  {
    m_adStream.stop();
    reset();
    enabled = encrypted = false;
  }
}

void Session::STREAM::reset()
{
  if (enabled)
  {
    m_streamReader.reset();
    m_streamFile.reset();
    m_adByteStream.reset();
    mainId_ = 0;
  }
}

Session::Session(const PROPERTIES::KodiProperties& kodiProps,
                 const std::string& url,
                 const std::map<std::string, std::string>& mediaHeaders,
                 const std::string& profilePath)
  : m_kodiProps(kodiProps),
    manifestURL_(url),
    media_headers_(mediaHeaders),
    m_dummySampleReader(new CDummySampleReader)
{
  // Create the representation chooser
  m_reprChooser = new adaptive::CRepresentationChooserDefault(profilePath);
  m_reprChooser->Initialize(kodiProps);

  // Create the adaptive tree
  switch (kodiProps.m_manifestType)
  {
    case PROPERTIES::ManifestType::MPD:
      adaptiveTree_ = new adaptive::DASHTree(kodiProps, m_reprChooser);
      break;
    case PROPERTIES::ManifestType::ISM:
      adaptiveTree_ = new adaptive::SmoothTree(kodiProps, m_reprChooser);
      break;
    case PROPERTIES::ManifestType::HLS:
      adaptiveTree_ =
          new adaptive::HLSTree(kodiProps, m_reprChooser, new AESDecrypter(kodiProps.m_licenseKey));
      break;
    default:
      LOG::LogF(LOGFATAL, "Manifest type not handled");
      return;
  };

  m_settingStreamSelection =
      static_cast<SETTINGS::StreamSelection>(kodi::addon::GetSettingInt("STREAMSELECTION"));
  LOG::Log(LOGDEBUG, "Setting STREAMSELECTION value: %d", m_settingStreamSelection);

  m_settingNoSecureDecoder = kodi::addon::GetSettingBoolean("NOSECUREDECODER");
  LOG::Log(LOGDEBUG, "Setting NOSECUREDECODER value: %d", m_settingNoSecureDecoder);

  switch (kodi::addon::GetSettingInt("MEDIATYPE"))
  {
    case 1:
      media_type_mask_ = static_cast<uint8_t>(1U) << adaptive::AdaptiveTree::AUDIO;
      break;
    case 2:
      media_type_mask_ = static_cast<uint8_t>(1U) << adaptive::AdaptiveTree::VIDEO;
      break;
    case 3:
      media_type_mask_ = (static_cast<uint8_t>(1U) << adaptive::AdaptiveTree::VIDEO) |
                         (static_cast<uint8_t>(1U) << adaptive::AdaptiveTree::SUBTITLE);
      break;
    default:
      media_type_mask_ = static_cast<uint8_t>(~0);
  }

  if (!kodiProps.m_serverCertificate.empty())
  {
    std::string decCert{ BASE64::Decode(kodiProps.m_serverCertificate) };
    server_certificate_.SetData(reinterpret_cast<const AP4_Byte*>(decCert.data()), decCert.size());
  }
}

Session::~Session()
{
  LOG::Log(LOGDEBUG, "Session::~Session()");
  m_streams.clear();

  DisposeDecrypter();

  delete adaptiveTree_;
  adaptiveTree_ = nullptr;

  delete m_reprChooser;
  m_reprChooser = nullptr;
}

void Session::GetSupportedDecrypterURN(std::string& key_system)
{
  typedef SSD::SSD_DECRYPTER* (*CreateDecryptorInstanceFunc)(SSD::SSD_HOST * host,
                                                             uint32_t version);

  std::string specialpath = kodi::addon::GetSettingString("DECRYPTERPATH");
  if (specialpath.empty())
  {
    LOG::Log(LOGDEBUG, "DECRYPTERPATH not specified in settings.xml");
    return;
  }
  kodihost->SetLibraryPath(kodi::vfs::TranslateSpecialProtocol(specialpath).c_str());

  std::vector<std::string> searchPaths(2);
  searchPaths[0] =
      kodi::vfs::TranslateSpecialProtocol("special://xbmcbinaddons/inputstream.adaptive/");
  searchPaths[1] = kodi::addon::GetAddonInfo("path");

  std::vector<kodi::vfs::CDirEntry> items;

  for (std::vector<std::string>::const_iterator path(searchPaths.begin());
       !decrypter_ && path != searchPaths.end(); ++path)
  {
    LOG::Log(LOGDEBUG, "Searching for decrypters in: %s", path->c_str());

    if (!kodi::vfs::GetDirectory(*path, "", items))
      continue;

    for (unsigned int i(0); i < items.size(); ++i)
    {
      if (strncmp(items[i].Label().c_str(), "ssd_", 4) &&
          strncmp(items[i].Label().c_str(), "libssd_", 7))
        continue;

      bool success = false;
      decrypterModule_ = new kodi::tools::CDllHelper;
      if (decrypterModule_->LoadDll(items[i].Path()))
        {
        CreateDecryptorInstanceFunc startup;
        if (decrypterModule_->RegisterSymbol(startup, "CreateDecryptorInstance"))
          {
            SSD::SSD_DECRYPTER* decrypter = startup(kodihost, SSD::SSD_HOST::version);
            const char* suppUrn(0);

            if (decrypter &&
                (suppUrn = decrypter->SelectKeySytem(m_kodiProps.m_licenseType.c_str())))
            {
              LOG::Log(LOGDEBUG, "Found decrypter: %s", items[i].Path().c_str());
              success = true;
              decrypter_ = decrypter;
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
        delete decrypterModule_;
        decrypterModule_ = 0;
      }
    }
  }
}

void Session::DisposeSampleDecrypter()
{
  if (decrypter_)
  {
    for (std::vector<CDMSESSION>::iterator b(cdm_sessions_.begin()), e(cdm_sessions_.end()); b != e;
         ++b)
    {
      b->cdm_session_str_ = nullptr;
      if (!b->shared_single_sample_decryptor_)
      {
        decrypter_->DestroySingleSampleDecrypter(b->single_sample_decryptor_);
        b->single_sample_decryptor_ = nullptr;
      }
      else
      {
        b->single_sample_decryptor_ = nullptr;
        b->shared_single_sample_decryptor_ = false;
      }
    }
  }
}

void Session::DisposeDecrypter()
{
  if (!decrypterModule_)
    return;

  DisposeSampleDecrypter();

  typedef void (*DeleteDecryptorInstanceFunc)(SSD::SSD_DECRYPTER*);
  DeleteDecryptorInstanceFunc disposefn;
  if (decrypterModule_->RegisterSymbol(disposefn, "DeleteDecryptorInstance"))
    disposefn(decrypter_);

  delete decrypterModule_;
  decrypterModule_ = 0;
  decrypter_ = 0;
}

/*----------------------------------------------------------------------
|   initialize
+---------------------------------------------------------------------*/

bool Session::Initialize(const std::uint8_t config)
{
  if (!adaptiveTree_)
    return false;

  // Get URN's wich are supported by this addon
  if (!m_kodiProps.m_licenseType.empty())
  {
    GetSupportedDecrypterURN(adaptiveTree_->supportedKeySystem_);
    LOG::Log(LOGDEBUG, "Supported URN: %s", adaptiveTree_->supportedKeySystem_.c_str());
  }

  // Preinitialize the DRM, if pre-initialisation data are provided
  std::map<std::string, std::string> additionalHeaders = std::map<std::string, std::string>();
  bool isSessionOpened{false};

  if (!m_kodiProps.m_drmPreInitData.empty())
  {
    std::string challengeB64;
    std::string sessionId;
    // Pre-initialize the DRM allow to generate the challenge and session ID data
    // used to make licensed manifest requests (via proxy callback)
    if (PreInitializeDRM(challengeB64, sessionId, isSessionOpened))
    {
      additionalHeaders["challengeB64"] = STRING::URLEncode(challengeB64);
      additionalHeaders["sessionId"] = sessionId;
    }
    else
      return false;
  }

  // Open manifest file with location redirect support  bool mpdSuccess;
  std::string manifestUrl =
      adaptiveTree_->location_.empty() ? manifestURL_ : adaptiveTree_->location_;
  if (!adaptiveTree_->open(manifestUrl, m_kodiProps.m_manifestUpdateParam, additionalHeaders) ||
      adaptiveTree_->empty())
  {
    LOG::Log(LOGERROR, "Could not open / parse manifest (%s)", manifestUrl.c_str());
    return false;
  }
  LOG::Log(LOGINFO,
            "Successfully parsed manifest file. #Periods: %ld, #Streams in first period: %ld, Type: "
            "%s, Download speed: %0.4f Bytes/s",
            adaptiveTree_->periods_.size(), adaptiveTree_->current_period_->adaptationSets_.size(),
            adaptiveTree_->has_timeshift_buffer_ ? "live" : "VOD",
            m_reprChooser->GetDownloadSpeed());

  drmConfig_ = config;

  // Always need at least 16s delay from live
  if (adaptiveTree_->live_delay_ < 16)
    adaptiveTree_->live_delay_ = 16;

  return InitializePeriod(isSessionOpened);
}

bool Session::PreInitializeDRM(std::string& challengeB64,
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

  cdm_sessions_.resize(2);
  memset(&cdm_sessions_.front(), 0, sizeof(CDMSESSION));
  // Try to initialize an SingleSampleDecryptor
  LOG::LogF(LOGDEBUG, "Entering encryption section");

  if (m_kodiProps.m_licenseKey.empty())
  {
    LOG::LogF(LOGERROR, "Invalid license_key");
    return false;
  }

  if (!decrypter_)
  {
    LOG::LogF(LOGERROR, "No decrypter found for encrypted stream");
    return false;
  }

  if (!decrypter_->HasCdmSession())
  {
    if (!decrypter_->OpenDRMSystem(m_kodiProps.m_licenseKey.c_str(), server_certificate_,
                                   drmConfig_))
    {
      LOG::LogF(LOGERROR, "OpenDRMSystem failed");
      return false;
    }
  }

  AP4_DataBuffer init_data;
  init_data.SetBufferSize(1024);
  const char* optionalKeyParameter(nullptr);

  // Set the provided PSSH
  std::string decPssh{BASE64::Decode(psshData)};
  init_data.SetData(reinterpret_cast<const AP4_Byte*>(decPssh.data()), decPssh.size());

  // Decode the provided KID
  std::string decKid{BASE64::Decode(kidData)};

  CDMSESSION& session(cdm_sessions_[1]);

  std::string hexKid{StringUtils::ToHexadecimal(decKid)};
  LOG::LogF(LOGDEBUG, "Initializing session with KID: %s", hexKid.c_str());

  if (decrypter_ && init_data.GetDataSize() >= 4 &&
      (session.single_sample_decryptor_ = decrypter_->CreateSingleSampleDecrypter(
           init_data, optionalKeyParameter, decKid, true)) != 0)
  {
    session.cdm_session_str_ = session.single_sample_decryptor_->GetSessionId();
    sessionId = session.cdm_session_str_;
    challengeB64 = decrypter_->GetChallengeB64Data(session.single_sample_decryptor_);
  }
  else
  {
    LOG::LogF(LOGERROR, "Initialize failed (SingleSampleDecrypter)");
    session.single_sample_decryptor_ = nullptr;
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

bool Session::InitializeDRM(bool addDefaultKID /* = false */)
{
  bool isSecureVideoSession = false;
  cdm_sessions_.resize(adaptiveTree_->current_period_->psshSets_.size());
  memset(&cdm_sessions_.front(), 0, sizeof(CDMSESSION));

  for (const Session::CDMSESSION& cdmsession : cdm_sessions_)
  {
    m_reprChooser->AddDecrypterCaps(cdmsession.decrypter_caps_);
  }

  // Try to initialize an SingleSampleDecryptor
  if (adaptiveTree_->current_period_->encryptionState_)
  {
    std::string licenseKey{m_kodiProps.m_licenseKey};

    if (licenseKey.empty())
      licenseKey = adaptiveTree_->license_url_;

    LOG::Log(LOGDEBUG, "Entering encryption section");

    if (licenseKey.empty())
    {
      LOG::Log(LOGERROR, "Invalid license_key");
      return false;
    }

    if (!decrypter_)
    {
      LOG::Log(LOGERROR, "No decrypter found for encrypted stream");
      return false;
    }

    if (!decrypter_->HasCdmSession())
    {
      if (!decrypter_->OpenDRMSystem(licenseKey.c_str(), server_certificate_, drmConfig_))
      {
        LOG::Log(LOGERROR, "OpenDRMSystem failed");
        return false;
      }
    }
    std::string strkey(adaptiveTree_->supportedKeySystem_.substr(9));
    size_t pos;
    while ((pos = strkey.find('-')) != std::string::npos)
      strkey.erase(pos, 1);
    if (strkey.size() != 32)
    {
      LOG::Log(LOGERROR, "Key system mismatch (%s)!",
                adaptiveTree_->supportedKeySystem_.c_str());
      return false;
    }

    unsigned char key_system[16];
    AP4_ParseHex(strkey.c_str(), key_system, 16);
    uint32_t currentSessionTypes = 0;

    for (size_t ses(1); ses < cdm_sessions_.size(); ++ses)
    {
      AP4_DataBuffer init_data;
      const char* optionalKeyParameter(nullptr);
      adaptive::AdaptiveTree::Period::PSSH sessionPsshset =
        adaptiveTree_->current_period_->psshSets_[ses];
      uint32_t sessionType = 0;

      if (sessionPsshset.media_ > 0)
      {
        sessionType = sessionPsshset.media_;
      }
      else
      {
        switch (sessionPsshset.adaptation_set_->type_)
        {
          case adaptive::AdaptiveTree::VIDEO:
            sessionType = adaptive::AdaptiveTree::Period::PSSH::MEDIA_VIDEO;
            break;
          case adaptive::AdaptiveTree::AUDIO:
            sessionType = adaptive::AdaptiveTree::Period::PSSH::MEDIA_AUDIO;
            break;
          default:
            break;
        }
      }

      if (sessionPsshset.pssh_ == "FILE")
      {
        LOG::Log(LOGDEBUG, "Searching PSSH data in FILE");

        if (m_kodiProps.m_licenseData.empty())
        {
          adaptive::AdaptiveTree::Representation* initialRepr{
              m_reprChooser->ChooseRepresentation(sessionPsshset.adaptation_set_)};

          Session::STREAM stream(*adaptiveTree_, sessionPsshset.adaptation_set_, initialRepr,
                                 media_headers_, m_reprChooser, m_kodiProps.m_playTimeshiftBuffer,
                                 false);

          stream.enabled = true;
          stream.m_adStream.start_stream();
          stream.SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream.m_adStream));
          stream.SetStreamFile(std::make_unique<AP4_File>(*stream.GetAdByteStream(),
                                                          AP4_DefaultAtomFactory::Instance_, true));
          AP4_Movie* movie = stream.GetStreamFile()->GetMovie();
          if (movie == NULL)
          {
            LOG::Log(LOGERROR, "No MOOV in stream!");
            stream.disable();
            return false;
          }
          AP4_Array<AP4_PsshAtom>& pssh = movie->GetPsshAtoms();

          for (unsigned int i = 0; !init_data.GetDataSize() && i < pssh.ItemCount(); i++)
          {
            if (memcmp(pssh[i].GetSystemId(), key_system, 16) == 0)
            {
              init_data.AppendData(pssh[i].GetData().GetData(), pssh[i].GetData().GetDataSize());
              if (sessionPsshset.defaultKID_.empty())
              {
                if (pssh[i].GetKid(0))
                  sessionPsshset.defaultKID_ =
                      std::string((const char*)pssh[i].GetKid(0), 16);
                else if (AP4_Track* track = movie->GetTrack(TIDC[stream.m_adStream.get_type()]))
                {
                  AP4_ProtectedSampleDescription* m_protectedDesc =
                      static_cast<AP4_ProtectedSampleDescription*>(track->GetSampleDescription(0));
                  AP4_ContainerAtom* schi;
                  if (m_protectedDesc->GetSchemeInfo() &&
                      (schi = m_protectedDesc->GetSchemeInfo()->GetSchiAtom()))
                  {
                    AP4_TencAtom* tenc(
                        AP4_DYNAMIC_CAST(AP4_TencAtom, schi->GetChild(AP4_ATOM_TYPE_TENC, 0)));
                    if (tenc)
                    {
                      sessionPsshset.defaultKID_ =
                          std::string(reinterpret_cast<const char*>(tenc->GetDefaultKid()), 16);
                    }
                    else
                    {
                      AP4_PiffTrackEncryptionAtom* piff(
                          AP4_DYNAMIC_CAST(AP4_PiffTrackEncryptionAtom,
                                           schi->GetChild(AP4_UUID_PIFF_TRACK_ENCRYPTION_ATOM, 0)));
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
            LOG::Log(LOGERROR,
                      "Could not extract license from video stream (PSSH not found)");
            stream.disable();
            return false;
          }
          stream.disable();
        }
        else if (!sessionPsshset.defaultKID_.empty())
        {
          init_data.SetData(reinterpret_cast<AP4_Byte*>(sessionPsshset.defaultKID_.data()), 16);

          std::string decLicenseData{BASE64::Decode(m_kodiProps.m_licenseData)};
          uint8_t* decLicDataUInt = reinterpret_cast<uint8_t*>(decLicenseData.data());

          uint8_t* uuid(reinterpret_cast<uint8_t*>(strstr(decLicenseData.data(), "{KID}")));
          if (uuid)
          {
            memmove(uuid + 11, uuid, m_kodiProps.m_licenseData.size() - (uuid - decLicDataUInt));
            memcpy(uuid, init_data.GetData(), init_data.GetDataSize());
            init_data.SetData(decLicDataUInt, m_kodiProps.m_licenseData.size() + 11);
          }
          else
            init_data.SetData(decLicDataUInt, m_kodiProps.m_licenseData.size());
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
            CreateISMlicense(sessionPsshset.defaultKID_,
                             licenseData, init_data_v);
            init_data.SetData(init_data_v.data(), init_data_v.size());
          }
          else
          {
            init_data.SetData(reinterpret_cast<const uint8_t*>(
                                  sessionPsshset.pssh_.data()),
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

      CDMSESSION& session(cdm_sessions_[ses]);
      std::string defaultKid{sessionPsshset.defaultKID_};

      const char* defkid = sessionPsshset.defaultKID_.empty()
                               ? nullptr
                               : sessionPsshset.defaultKID_.data();

      if (addDefaultKID && ses == 1 && session.single_sample_decryptor_)
      {
        // If the CDM has been pre-initialized, on non-android systems
        // we use the same session opened then we have to add the current KID
        // because the session has been opened with a different PSSH/KID
        session.single_sample_decryptor_->AddKeyId(defaultKid);
        session.single_sample_decryptor_->SetDefaultKeyId(defaultKid);
      }

      if (decrypter_ && !defaultKid.empty())
      {
        std::string hexKid{StringUtils::ToHexadecimal(defaultKid)};
        LOG::Log(LOGDEBUG, "Initializing stream with KID: %s", hexKid.c_str());

        // use shared ssd session if we already have 1 of the same stream type
        if (currentSessionTypes & sessionType)
        {
          for (unsigned int i(1); i < ses; ++i)
          {
            if (decrypter_->HasLicenseKey(cdm_sessions_[i].single_sample_decryptor_,
                                          reinterpret_cast<const uint8_t*>(defkid)))
            {
              session.single_sample_decryptor_ = cdm_sessions_[i].single_sample_decryptor_;
              session.shared_single_sample_decryptor_ = true;
              break;
            }
          }
        }
      }
      else if (!defkid && !session.single_sample_decryptor_)
      {
          LOG::Log(LOGWARNING, "Initializing stream with unknown KID!");
      }

      currentSessionTypes |= sessionType;

      if (decrypter_ && init_data.GetDataSize() >= 4 &&
          (session.single_sample_decryptor_ ||
           (session.single_sample_decryptor_ = decrypter_->CreateSingleSampleDecrypter(
                init_data, optionalKeyParameter, defaultKid, false)) != 0))
      {
        decrypter_->GetCapabilities(
            session.single_sample_decryptor_, reinterpret_cast<const uint8_t*>(defaultKid.c_str()),
            sessionPsshset.media_, session.decrypter_caps_);

        if (session.decrypter_caps_.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_INVALID)
          adaptiveTree_->current_period_->RemovePSSHSet(static_cast<std::uint16_t>(ses));
        else if (session.decrypter_caps_.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH)
        {
          session.cdm_session_str_ = session.single_sample_decryptor_->GetSessionId();
          isSecureVideoSession = true;

          if (m_settingNoSecureDecoder && !m_kodiProps.m_isLicenseForceSecureDecoder &&
              !adaptiveTree_->current_period_->need_secure_decoder_)
            session.decrypter_caps_.flags &= ~SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_DECODER;
        }
      }
      else
      {
        LOG::Log(LOGERROR, "Initialize failed (SingleSampleDecrypter)");
        for (unsigned int i(ses); i < cdm_sessions_.size(); ++i)
          cdm_sessions_[i].single_sample_decryptor_ = nullptr;
        return false;
      }
    }
  }
  m_reprChooser->SetSecureSession(isSecureVideoSession);
  m_reprChooser->PostInit();

  return true;
}

bool Session::InitializePeriod(bool isSessionOpened /* = false */)
{
  bool psshChanged = true;
  if (adaptiveTree_->next_period_)
  {
    psshChanged =
        !(adaptiveTree_->current_period_->psshSets_ == adaptiveTree_->next_period_->psshSets_);
    adaptiveTree_->current_period_ = adaptiveTree_->next_period_;
    adaptiveTree_->next_period_ = nullptr;
  }

  chapter_start_time_ = GetChapterStartTime();

  if (adaptiveTree_->current_period_->encryptionState_ ==
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

  while ((adp = adaptiveTree_->GetAdaptationSet(adpIndex++)))
  {
    if (adp->representations_.empty())
      continue;

    bool isManualStreamSelection;
    if (adp->type_ == adaptive::AdaptiveTree::StreamType::VIDEO)
      isManualStreamSelection = m_settingStreamSelection != SETTINGS::StreamSelection::AUTO;
    else
      isManualStreamSelection = m_settingStreamSelection == SETTINGS::StreamSelection::MANUAL;

    // Get the default initial stream repr. based on "adaptive repr. chooser"
    adaptive::AdaptiveTree::Representation* defaultRepr{adaptiveTree_->GetRepChooser()->ChooseRepresentation(adp)};

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

  first_period_initialized_ = true;
  return true;
}

void Session::AddStream(adaptive::AdaptiveTree::AdaptationSet* adp,
                        adaptive::AdaptiveTree::Representation* initialRepr,
                        bool isDefaultRepr,
                        uint32_t uniqueId)
{
  m_streams.push_back(std::make_unique<STREAM>(*adaptiveTree_, adp, initialRepr, media_headers_,
                                               m_reprChooser, m_kodiProps.m_playTimeshiftBuffer,
                                               first_period_initialized_));

  STREAM& stream(*m_streams.back());

  uint32_t flags = INPUTSTREAM_FLAG_NONE;
  size_t copySize = adp->name_.size() > 255 ? 255 : adp->name_.size();
  stream.info_.SetName(adp->name_);

  switch (adp->type_)
  {
    case adaptive::AdaptiveTree::VIDEO:
    {
      stream.info_.SetStreamType(INPUTSTREAM_TYPE_VIDEO);
      if (isDefaultRepr)
        flags |= INPUTSTREAM_FLAG_DEFAULT;
      break;
    }
    case adaptive::AdaptiveTree::AUDIO:
    {
      stream.info_.SetStreamType(INPUTSTREAM_TYPE_AUDIO);
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
      stream.info_.SetStreamType(INPUTSTREAM_TYPE_SUBTITLE);
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

  stream.info_.SetFlags(flags);
  stream.info_.SetPhysicalIndex(uniqueId);
  stream.info_.SetLanguage(adp->language_);
  stream.info_.ClearExtraData();
  stream.info_.SetFeatures(0);

  stream.m_adStream.set_observer(dynamic_cast<adaptive::AdaptiveStreamObserver*>(this));

  UpdateStream(stream);
}

void Session::UpdateStream(STREAM& stream)
{
  const adaptive::AdaptiveTree::Representation* rep(stream.m_adStream.getRepresentation());
  const SSD::SSD_DECRYPTER::SSD_CAPS& caps = GetDecrypterCaps(rep->pssh_set_);

  stream.info_.SetWidth(static_cast<uint32_t>(rep->width_));
  stream.info_.SetHeight(static_cast<uint32_t>(rep->height_));
  stream.info_.SetAspect(rep->aspect_);

  if (stream.info_.GetAspect() == 0.0f && stream.info_.GetHeight())
    stream.info_.SetAspect((float)stream.info_.GetWidth() / stream.info_.GetHeight());
  stream.encrypted = rep->get_psshset() > 0;

  stream.info_.SetExtraData(nullptr, 0);
  if (rep->codec_private_data_.size())
  {
    std::string annexb;
    const std::string* res(&annexb);

    if ((caps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_ANNEXB_REQUIRED) &&
        stream.info_.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
    {
      LOG::Log(LOGDEBUG, "UpdateStream: Convert avc -> annexb");
      annexb = AvcToAnnexb(rep->codec_private_data_);
    }
    else
      res = &rep->codec_private_data_;

    stream.info_.SetExtraData(reinterpret_cast<const uint8_t*>(res->data()), res->size());
  }

  // we currently use only the first track!
  std::string::size_type pos = rep->codecs_.find(",");
  if (pos == std::string::npos)
    pos = rep->codecs_.size();

  stream.info_.SetCodecInternalName(rep->codecs_);
  stream.info_.SetCodecFourCC(0);

#if INPUTSTREAM_VERSION_LEVEL > 0
  stream.info_.SetColorSpace(INPUTSTREAM_COLORSPACE_UNSPECIFIED);
  stream.info_.SetColorRange(INPUTSTREAM_COLORRANGE_UNKNOWN);
  stream.info_.SetColorPrimaries(INPUTSTREAM_COLORPRIMARY_UNSPECIFIED);
  stream.info_.SetColorTransferCharacteristic(INPUTSTREAM_COLORTRC_UNSPECIFIED);
#else
  stream.info_.SetColorSpace(INPUTSTREAM_COLORSPACE_UNKNOWN);
  stream.info_.SetColorRange(INPUTSTREAM_COLORRANGE_UNKNOWN);
#endif
  if (rep->codecs_.find("mp4a") == 0 || rep->codecs_.find("aac") == 0)
    stream.info_.SetCodecName("aac");
  else if (rep->codecs_.find("dts") == 0)
    stream.info_.SetCodecName("dca");
  else if (rep->codecs_.find("ac-3") == 0)
    stream.info_.SetCodecName("ac3");
  else if (rep->codecs_.find("ec-3") == 0)
    stream.info_.SetCodecName("eac3");
  else if (rep->codecs_.find("avc") == 0 || rep->codecs_.find("h264") == 0)
    stream.info_.SetCodecName("h264");
  else if (rep->codecs_.find("hev") == 0)
    stream.info_.SetCodecName("hevc");
  else if (rep->codecs_.find("hvc") == 0 || rep->codecs_.find("dvh") == 0)
  {
    stream.info_.SetCodecFourCC(
        MakeFourCC(rep->codecs_[0], rep->codecs_[1], rep->codecs_[2], rep->codecs_[3]));
    stream.info_.SetCodecName("hevc");
  }
  else if (rep->codecs_.find("vp9") == 0 || rep->codecs_.find("vp09") == 0)
  {
    stream.info_.SetCodecName("vp9");
#if INPUTSTREAM_VERSION_LEVEL > 0
    if ((pos = rep->codecs_.find(".")) != std::string::npos)
      stream.info_.SetCodecProfile(static_cast<STREAMCODEC_PROFILE>(
          VP9CodecProfile0 + atoi(rep->codecs_.c_str() + (pos + 1))));
#endif
  }
  else if (rep->codecs_.find("opus") == 0)
    stream.info_.SetCodecName("opus");
  else if (rep->codecs_.find("vorbis") == 0)
    stream.info_.SetCodecName("vorbis");
  else if (rep->codecs_.find("stpp") == 0 || rep->codecs_.find("ttml") == 0)
    stream.info_.SetCodecName("srt");
  else if (rep->codecs_.find("wvtt") == 0)
    stream.info_.SetCodecName("webvtt");
  else
  {
    LOG::Log(LOGWARNING, "Unsupported codec %s in stream ID: %s", rep->codecs_.c_str(),
             rep->id.c_str());
    stream.valid = false;
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
    stream.valid = false;
  }

  stream.info_.SetFpsRate(rep->fpsRate_);
  stream.info_.SetFpsScale(rep->fpsScale_);
  stream.info_.SetSampleRate(rep->samplingRate_);
  stream.info_.SetChannels(rep->channelCount_);
  stream.info_.SetBitRate(rep->bandwidth_);
}

AP4_Movie* Session::PrepareStream(STREAM* stream, bool& needRefetch)
{
  needRefetch = false;
  switch (adaptiveTree_->prepareRepresentation(stream->m_adStream.getPeriod(),
                                               stream->m_adStream.getAdaptationSet(),
                                               stream->m_adStream.getRepresentation()))
  {
    case adaptive::AdaptiveTree::PREPARE_RESULT_FAILURE:
      return nullptr;
    case adaptive::AdaptiveTree::PREPARE_RESULT_DRMCHANGED:
      if (!InitializeDRM())
        return nullptr;
    case adaptive::AdaptiveTree::PREPARE_RESULT_DRMUNCHANGED:
      stream->encrypted = stream->m_adStream.getRepresentation()->pssh_set_ > 0;
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
    AP4_Movie* movie = new AP4_Movie();

    AP4_SyntheticSampleTable* sample_table = new AP4_SyntheticSampleTable();

    AP4_SampleDescription* sample_descryption;
    if (stream->info_.GetCodecName() == "h264")
    {
      const std::string& extradata(stream->m_adStream.getRepresentation()->codec_private_data_);
      AP4_MemoryByteStream ms((const uint8_t*)extradata.data(), extradata.size());
      AP4_AvccAtom* atom = AP4_AvccAtom::Create(AP4_ATOM_HEADER_SIZE + extradata.size(), ms);
      sample_descryption =
          new AP4_AvcSampleDescription(AP4_SAMPLE_FORMAT_AVC1, stream->info_.GetWidth(),
                                       stream->info_.GetHeight(), 0, nullptr, atom);
    }
    else if (stream->info_.GetCodecName() == "hevc")
    {
      const std::string& extradata(stream->m_adStream.getRepresentation()->codec_private_data_);
      AP4_MemoryByteStream ms((const uint8_t*)extradata.data(), extradata.size());
      AP4_HvccAtom* atom = AP4_HvccAtom::Create(AP4_ATOM_HEADER_SIZE + extradata.size(), ms);
      sample_descryption =
          new AP4_HevcSampleDescription(AP4_SAMPLE_FORMAT_HEV1, stream->info_.GetWidth(),
                                        stream->info_.GetHeight(), 0, nullptr, atom);
    }
    else if (stream->info_.GetCodecName() == "srt")
      sample_descryption = new AP4_SampleDescription(AP4_SampleDescription::TYPE_SUBTITLES,
                                                     AP4_SAMPLE_FORMAT_STPP, 0);
    else
      sample_descryption = new AP4_SampleDescription(AP4_SampleDescription::TYPE_UNKNOWN, 0, 0);

    if (stream->m_adStream.getRepresentation()->get_psshset() > 0)
    {
      AP4_ContainerAtom schi(AP4_ATOM_TYPE_SCHI);
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
    AP4_MoovAtom* moov = new AP4_MoovAtom();
    moov->AddChild(new AP4_ContainerAtom(AP4_ATOM_TYPE_MVEX));
    movie->SetMoovAtom(moov);
    return movie;
  }
  return nullptr;
}

void Session::EnableStream(STREAM* stream, bool enable)
{
  if (enable)
  {
    if (!timing_stream_)
      timing_stream_ = stream;
    stream->enabled = true;
  }
  else
  {
    if (stream == timing_stream_)
      timing_stream_ = nullptr;
    stream->disable();
  }
}

uint64_t Session::PTSToElapsed(uint64_t pts)
{
  if (timing_stream_)
  {
    ISampleReader* timingReader = timing_stream_->GetReader();
    if (!timingReader) {
      LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
      return 0;
    }

    int64_t manifest_time = static_cast<int64_t>(pts) - timingReader->GetPTSDiff();
    if (manifest_time < 0)
      manifest_time = 0;

    if (static_cast<uint64_t>(manifest_time) > timing_stream_->m_adStream.GetAbsolutePTSOffset())
      return static_cast<uint64_t>(manifest_time) - timing_stream_->m_adStream.GetAbsolutePTSOffset();

    return 0ULL;
  }
  else
    return pts;
}

uint64_t Session::GetTimeshiftBufferStart()
{
  if (timing_stream_)
  {
    ISampleReader* timingReader = timing_stream_->GetReader();
    if (!timingReader)
    {
      LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
      return 0;
    }
    return timing_stream_->m_adStream.GetAbsolutePTSOffset() + timingReader->GetPTSDiff();
  }
  else
    return 0ULL;
}

void Session::StartReader(
    STREAM* stream, uint64_t seekTimeCorrected, int64_t ptsDiff, bool preceeding, bool timing)
{
  bool bReset = true;
  if (timing)
    seekTimeCorrected += stream->m_adStream.GetAbsolutePTSOffset();
  else
    seekTimeCorrected -= ptsDiff;
  stream->m_adStream.seek_time(
      static_cast<double>(seekTimeCorrected / STREAM_TIME_BASE),
      preceeding, bReset);

  ISampleReader* streamReader = stream->GetReader();
  if (!streamReader) {
    LOG::LogF(LOGERROR, "Cannot get the stream reader");
    return;
  }

  if (bReset)
    streamReader->Reset(false);

  bool bStarted = false;
  streamReader->Start(bStarted);
  if (bStarted && (streamReader->GetInformation(stream->info_)))
    changed_ = true;
}

void Session::SetVideoResolution(int width, int height)
{
  m_reprChooser->SetScreenResolution(width, height);
};

ISampleReader* Session::GetNextSample()
{
  STREAM* res{nullptr};
  STREAM* waiting{nullptr};

  for (auto& stream : m_streams)
  {
    bool isStarted{false};
    ISampleReader* streamReader = stream->GetReader();
    if (!streamReader)
      continue;

    if (stream->enabled && !streamReader->EOS() && AP4_SUCCEEDED(streamReader->Start(isStarted)))
    {
      if (!res || streamReader->DTSorPTS() < res->GetReader()->DTSorPTS())
      {
        if (stream->m_adStream.waitingForSegment(true))
          waiting = stream.get();
        else
          res = stream.get();
      }
    }

    if (isStarted && streamReader->GetInformation(stream->info_))
      changed_ = true;
  }

  if (res)
  {
    CheckFragmentDuration(*res);
    if (res->GetReader()->GetInformation(res->info_))
      changed_ = true;
    if (res->GetReader()->PTS() != STREAM_NOPTS_VALUE)
      elapsed_time_ = PTSToElapsed(res->GetReader()->PTS()) + GetChapterStartTime();
    return res->GetReader();
  }
  else if (waiting)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return m_dummySampleReader.get();
  }
  return nullptr;
}

bool Session::SeekTime(double seekTime, unsigned int streamId, bool preceeding)
{
  bool ret(false);

  //we don't have pts < 0 here and work internally with uint64
  if (seekTime < 0)
    seekTime = 0;

  // Check if we leave our current period
  double chapterTime(0);
  std::vector<adaptive::AdaptiveTree::Period*>::const_iterator pi;
  for (pi = adaptiveTree_->periods_.cbegin(); pi != adaptiveTree_->periods_.cend(); ++pi)
  {
    chapterTime += double((*pi)->duration_) / (*pi)->timescale_;
    if (chapterTime > seekTime)
      break;
  }

  if (pi == adaptiveTree_->periods_.end())
    --pi;
  chapterTime -= double((*pi)->duration_) / (*pi)->timescale_;

  if ((*pi) != adaptiveTree_->current_period_)
  {
    LOG::Log(LOGDEBUG, "SeekTime: seeking into new chapter: %d",
              static_cast<int>((pi - adaptiveTree_->periods_.begin()) + 1));
    SeekChapter((pi - adaptiveTree_->periods_.begin()) + 1);
    chapter_seek_time_ = seekTime;
    return true;
  }

  seekTime -= chapterTime;

  // don't try to seek past the end of the stream, leave a sensible amount so we can buffer properly
  if (adaptiveTree_->has_timeshift_buffer_)
  {
    double maxSeek(0);
    uint64_t curTime, maxTime(0);
    for (auto& stream : m_streams)
    {
      if (stream->enabled && (curTime = stream->m_adStream.getMaxTimeMs()) && curTime > maxTime)
      {
        maxTime = curTime;
      }
    }

    maxSeek = (static_cast<double>(maxTime) / 1000) - adaptiveTree_->live_delay_;
    if (maxSeek < 0)
      maxSeek = 0;

    if (seekTime > maxSeek)
      seekTime = maxSeek;
  }

  // correct for starting segment pts value of chapter and chapter offset within program
  uint64_t seekTimeCorrected = static_cast<uint64_t>(seekTime * STREAM_TIME_BASE);
  int64_t ptsDiff = 0;
  if (timing_stream_)
  {
    // after seeking across chapters with fmp4 streams the reader will not have started
    // so we start here to ensure that we have the required information to correctly
    // seek with proper stream alignment
    ISampleReader* timingReader = timing_stream_->GetReader();
    if (!timingReader)
    {
      LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
      return false;
    }
    if (!timingReader->IsStarted())
      StartReader(timing_stream_, seekTimeCorrected, ptsDiff, preceeding, true);

    seekTimeCorrected += timing_stream_->m_adStream.GetAbsolutePTSOffset();
    ptsDiff = timingReader->GetPTSDiff();
    if (ptsDiff < 0 && seekTimeCorrected + ptsDiff > seekTimeCorrected)
      seekTimeCorrected = 0;
    else
      seekTimeCorrected += ptsDiff;
  }

  for (auto& stream : m_streams)
  {
    ISampleReader* streamReader = stream->GetReader();
    if (!streamReader)
    {
      LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
      continue;
    }
    if (stream->enabled && (streamId == 0 || stream->info_.GetPhysicalIndex() == streamId))
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
          LOG::Log(LOGINFO, "Seek time (%0.1lf) for stream: %d continues at %0.1lf (PTS: %llu)",
                   seekTime, stream->info_.GetPhysicalIndex(), destTime, streamReader->PTS());
          if (stream->info_.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
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

void Session::OnSegmentChanged(adaptive::AdaptiveStream* adStream)
{
  for (auto& stream : m_streams)
  {
    if (&stream->m_adStream == adStream)
    {
      ISampleReader* streamReader = stream->GetReader();
      if (!streamReader)
        LOG::LogF(LOGWARNING, "Cannot get the stream sample reader");
      else
        streamReader->SetPTSOffset(stream->m_adStream.GetCurrentPTSOffset());

      stream->segmentChanged = true;
      break;
    }
  }
}

void Session::OnStreamChange(adaptive::AdaptiveStream* adStream)
{
  for (auto& stream : m_streams)
  {
    if (stream->enabled && &stream->m_adStream == adStream)
    {
      UpdateStream(*stream);
      changed_ = true;
    }
  }
}

void Session::CheckFragmentDuration(STREAM& stream)
{
  uint64_t nextTs;
  uint64_t nextDur;
  ISampleReader* streamReader = stream.GetReader();
  if (!streamReader)
  {
    LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
    return;
  }

  if (stream.segmentChanged && streamReader->GetNextFragmentInfo(nextTs, nextDur))
  {
    adaptiveTree_->SetFragmentDuration(
        stream.m_adStream.getAdaptationSet(), stream.m_adStream.getRepresentation(),
        stream.m_adStream.getSegmentPos(), nextTs, static_cast<uint32_t>(nextDur),
        streamReader->GetTimeScale());
  }
  stream.segmentChanged = false;
}

const AP4_UI08* Session::GetDefaultKeyId(const uint16_t index) const
{
  static const AP4_UI08 default_key[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  if (adaptiveTree_->current_period_->psshSets_[index].defaultKID_.size() == 16)
    return reinterpret_cast<const AP4_UI08*>(
        adaptiveTree_->current_period_->psshSets_[index].defaultKID_.data());
  return default_key;
}

Adaptive_CencSingleSampleDecrypter* Session::GetSingleSampleDecrypter(std::string sessionId)
{
  for (std::vector<CDMSESSION>::iterator b(cdm_sessions_.begin() + 1), e(cdm_sessions_.end());
       b != e; ++b)
    if (b->cdm_session_str_ && sessionId == b->cdm_session_str_)
      return b->single_sample_decryptor_;
  return nullptr;
}

uint32_t Session::GetIncludedStreamMask() const
{
  const INPUTSTREAM_TYPE adp2ips[] = {
      INPUTSTREAM_TYPE_NONE, INPUTSTREAM_TYPE_VIDEO, INPUTSTREAM_TYPE_AUDIO,
      INPUTSTREAM_TYPE_SUBTITLE};
  uint32_t res(0);
  for (unsigned int i(0); i < 4; ++i)
    if (adaptiveTree_->current_period_->included_types_ & (1U << i))
      res |= (1U << adp2ips[i]);
  return res;
}

STREAM_CRYPTO_KEY_SYSTEM Session::GetCryptoKeySystem() const
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

int Session::GetChapter() const
{
  if (adaptiveTree_)
  {
    std::vector<adaptive::AdaptiveTree::Period*>::const_iterator res =
        std::find(adaptiveTree_->periods_.cbegin(), adaptiveTree_->periods_.cend(),
                  adaptiveTree_->current_period_);
    if (res != adaptiveTree_->periods_.cend())
      return (res - adaptiveTree_->periods_.cbegin()) + 1;
  }
  return -1;
}

int Session::GetChapterCount() const
{
  if (adaptiveTree_)
    return adaptiveTree_->periods_.size() > 1 ? adaptiveTree_->periods_.size() : 0;
  return 0;
}

const char* Session::GetChapterName(int ch) const
{
  --ch;
  if (ch >= 0 && ch < static_cast<int>(adaptiveTree_->periods_.size()))
    return adaptiveTree_->periods_[ch]->id_.c_str();
  return "[Unknown]";
}

int64_t Session::GetChapterPos(int ch) const
{
  int64_t sum(0);
  --ch;

  for (; ch; --ch)
    sum += (adaptiveTree_->periods_[ch - 1]->duration_ * STREAM_TIME_BASE) /
           adaptiveTree_->periods_[ch - 1]->timescale_;
  return sum / STREAM_TIME_BASE;
}

uint64_t Session::GetChapterStartTime() const
{
  uint64_t start_time = 0;
  for (adaptive::AdaptiveTree::Period* p : adaptiveTree_->periods_)
    if (p == adaptiveTree_->current_period_)
      break;
    else
      start_time += (p->duration_ * STREAM_TIME_BASE) / p->timescale_;
  return start_time;
}

int Session::GetPeriodId() const
{
  if (adaptiveTree_)
  {
    if (IsLive())
      return adaptiveTree_->current_period_->sequence_ == adaptiveTree_->initial_sequence_
                 ? 1
                 : adaptiveTree_->current_period_->sequence_ + 1;
    else
      return GetChapter();
  }
  return -1;
}

bool Session::SeekChapter(int ch)
{
  if (adaptiveTree_->next_period_)
    return true;

  --ch;
  if (ch >= 0 && ch < static_cast<int>(adaptiveTree_->periods_.size()) &&
      adaptiveTree_->periods_[ch] != adaptiveTree_->current_period_)
  {
    adaptiveTree_->next_period_ = adaptiveTree_->periods_[ch];
    for (auto& stream : m_streams)
    {
      if (stream->GetReader())
        stream->GetReader()->Reset(true);
    }
    return true;
  }

  return false;
}

/***************************  Interface *********************************/

class CInputStreamAdaptive;

/*******************************************************/
/*                     VideoCodec                      */
/*******************************************************/

class ATTR_DLL_LOCAL CVideoCodecAdaptive : public kodi::addon::CInstanceVideoCodec
{
public:
  CVideoCodecAdaptive(const kodi::addon::IInstanceInfo& instance);
  CVideoCodecAdaptive(const kodi::addon::IInstanceInfo& instance, CInputStreamAdaptive* parent);
  virtual ~CVideoCodecAdaptive();

  bool Open(const kodi::addon::VideoCodecInitdata& initData) override;
  bool Reconfigure(const kodi::addon::VideoCodecInitdata& initData) override;
  bool AddData(const DEMUX_PACKET& packet) override;
  VIDEOCODEC_RETVAL GetPicture(VIDEOCODEC_PICTURE& picture) override;
  const char* GetName() override { return m_name.c_str(); };
  void Reset() override;

private:
  enum STATE : unsigned int
  {
    STATE_WAIT_EXTRADATA = 1
  };

  std::shared_ptr<Session> m_session;
  unsigned int m_state;
  std::string m_name;
};

/*******************************************************/
/*                     InputStream                     */
/*******************************************************/

class ATTR_DLL_LOCAL CInputStreamAdaptive : public kodi::addon::CInstanceInputStream
{
public:
  CInputStreamAdaptive(const kodi::addon::IInstanceInfo& instance);
  ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo& instance,
                              KODI_ADDON_INSTANCE_HDL& hdl) override;

  bool Open(const kodi::addon::InputstreamProperty& props) override;
  void Close() override;
  bool GetStreamIds(std::vector<unsigned int>& ids) override;
  void GetCapabilities(kodi::addon::InputstreamCapabilities& caps) override;
  bool GetStream(int streamid, kodi::addon::InputstreamInfo& info) override;
  void EnableStream(int streamid, bool enable) override;
  bool OpenStream(int streamid) override;
  DEMUX_PACKET* DemuxRead() override;
  bool DemuxSeekTime(double time, bool backwards, double& startpts) override;
  void SetVideoResolution(int width, int height) override;
  bool PosTime(int ms) override;
  int GetTotalTime() override;
  int GetTime() override;
  bool IsRealTimeStream() override;

#if INPUTSTREAM_VERSION_LEVEL > 1
  int GetChapter() override;
  int GetChapterCount() override;
  const char* GetChapterName(int ch) override;
  int64_t GetChapterPos(int ch) override;
  bool SeekChapter(int ch) override;
#endif

  std::shared_ptr<Session> GetSession() { return m_session; };

private:
  std::shared_ptr<Session> m_session{nullptr};
  UTILS::PROPERTIES::KodiProperties m_kodiProps;
  int m_currentVideoWidth{1280};
  int m_currentVideoHeight{720};
  uint32_t m_IncludedStreams[16];
  bool m_checkChapterSeek = false;
  int m_failedSeekTime = ~0;

  void UnlinkIncludedStreams(Session::STREAM* stream);
};

CInputStreamAdaptive::CInputStreamAdaptive(const kodi::addon::IInstanceInfo& instance)
  : CInstanceInputStream(instance)
{
  memset(m_IncludedStreams, 0, sizeof(m_IncludedStreams));
}

ADDON_STATUS CInputStreamAdaptive::CreateInstance(const kodi::addon::IInstanceInfo& instance,
                                                  KODI_ADDON_INSTANCE_HDL& hdl)
{
  if (instance.IsType(ADDON_INSTANCE_VIDEOCODEC))
  {
    hdl = new CVideoCodecAdaptive(instance, this);
    return ADDON_STATUS_OK;
  }
  return ADDON_STATUS_NOT_IMPLEMENTED;
}

bool CInputStreamAdaptive::Open(const kodi::addon::InputstreamProperty& props)
{
  LOG::Log(LOGDEBUG, "Open()");

  std::string url = props.GetURL();
  m_kodiProps = PROPERTIES::ParseKodiProperties(props.GetProperties());

  if (m_kodiProps.m_manifestType == PROPERTIES::ManifestType::UNKNOWN)
    return false;

  std::uint8_t drmConfig{0};
  if (m_kodiProps.m_isLicensePersistentStorage)
    drmConfig |= SSD::SSD_DECRYPTER::CONFIG_PERSISTENTSTORAGE;

  std::map<std::string, std::string> mediaHeaders{m_kodiProps.m_streamHeaders};

  // If the URL contains headers then replace the stream headers
  std::string::size_type posHeader(url.find("|"));
  if (posHeader != std::string::npos)
  {
    m_kodiProps.m_streamHeaders.clear();
    ParseHeaderString(m_kodiProps.m_streamHeaders, url.substr(posHeader + 1));
    url = url.substr(0, posHeader);
  }
  if (mediaHeaders.empty())
    mediaHeaders = m_kodiProps.m_streamHeaders;

  kodihost->SetProfilePath(props.GetProfileFolder());

  m_session = std::make_shared<Session>(m_kodiProps, url, mediaHeaders, props.GetProfileFolder());
  m_session->SetVideoResolution(m_currentVideoWidth, m_currentVideoHeight);

  if (!m_session->Initialize(drmConfig))
  {
    m_session = nullptr;
    return false;
  }
  return true;
}

void CInputStreamAdaptive::Close(void)
{
  LOG::Log(LOGDEBUG, "Close()");
  m_session = nullptr;
}

bool CInputStreamAdaptive::GetStreamIds(std::vector<unsigned int>& ids)
{
  LOG::Log(LOGDEBUG, "GetStreamIds()");
  INPUTSTREAM_IDS iids;

  if (m_session)
  {
    adaptive::AdaptiveTree::Period* period;
    int period_id = m_session->GetPeriodId();
    iids.m_streamCount = 0;
    unsigned int id;

    for (unsigned int i(1); i <= INPUTSTREAM_MAX_STREAM_COUNT && i <= m_session->GetStreamCount();
         ++i)
    {
      Session::STREAM* stream = m_session->GetStream(i);
      if (!stream)
      {
        LOG::LogF(LOGERROR, "Cannot get the stream from sid %u", i);
        continue;
      }

      uint8_t cdmId(static_cast<uint8_t>(stream->m_adStream.getRepresentation()->pssh_set_));
      if (stream->valid &&
          (m_session->GetMediaTypeMask() & static_cast<uint8_t>(1) << stream->m_adStream.get_type()))
      {
        if (m_session->GetMediaTypeMask() != 0xFF)
        {
          const adaptive::AdaptiveTree::Representation* rep(stream->m_adStream.getRepresentation());
          if (rep->flags_ & adaptive::AdaptiveTree::Representation::INCLUDEDSTREAM)
            continue;
        }
        if (m_session->IsLive())
        {
          period = stream->m_adStream.getPeriod();
          if (period->sequence_ == m_session->GetInitialSequence())
          {
            id = i + 1000;
          }
          else
          {
            id = i + (period->sequence_ + 1) * 1000;
          }
        }
        else
        {
          id = i + period_id * 1000;
        }
        ids.emplace_back(id);
      }
    }
  }

  return !ids.empty();
}

void CInputStreamAdaptive::GetCapabilities(kodi::addon::InputstreamCapabilities& caps)
{
  LOG::Log(LOGDEBUG, "GetCapabilities()");
  uint32_t mask = INPUTSTREAM_SUPPORTS_IDEMUX | INPUTSTREAM_SUPPORTS_IDISPLAYTIME |
                  INPUTSTREAM_SUPPORTS_IPOSTIME | INPUTSTREAM_SUPPORTS_SEEK |
                  INPUTSTREAM_SUPPORTS_PAUSE;
#if INPUTSTREAM_VERSION_LEVEL > 1
  mask |= INPUTSTREAM_SUPPORTS_ICHAPTER;
#endif
  caps.SetMask(mask);
}

bool CInputStreamAdaptive::GetStream(int streamid, kodi::addon::InputstreamInfo& info)
{
  LOG::Log(LOGDEBUG, "GetStream(%d)", streamid);

  Session::STREAM* stream(m_session->GetStream(streamid - m_session->GetPeriodId() * 1000));

  if (stream)
  {
    uint8_t cdmId(static_cast<uint8_t>(stream->m_adStream.getRepresentation()->pssh_set_));
    if (stream->encrypted && m_session->GetCDMSession(cdmId) != nullptr)
    {
      kodi::addon::StreamCryptoSession cryptoSession;

      LOG::Log(LOGDEBUG, "GetStream(%d): initalizing crypto session", streamid);
      cryptoSession.SetKeySystem(m_session->GetCryptoKeySystem());

      const char* sessionId(m_session->GetCDMSession(cdmId));
      cryptoSession.SetSessionId(sessionId);

      if (m_session->GetDecrypterCaps(cdmId).flags &
          SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SUPPORTS_DECODING)
        stream->info_.SetFeatures(INPUTSTREAM_FEATURE_DECODE);
      else
        stream->info_.SetFeatures(0);

      cryptoSession.SetFlags((m_session->GetDecrypterCaps(cdmId).flags &
                          SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_DECODER)
                             ? STREAM_CRYPTO_FLAG_SECURE_DECODER
                             : 0);
      stream->info_.SetCryptoSession(cryptoSession);
    }

    info = stream->info_;
    return true;
  }

  return false;
}

void CInputStreamAdaptive::UnlinkIncludedStreams(Session::STREAM* stream)
{
  if (stream->mainId_)
  {
    Session::STREAM* mainStream(m_session->GetStream(stream->mainId_));
    if (mainStream->GetReader())
      mainStream->GetReader()->RemoveStreamType(stream->info_.GetStreamType());
  }
  const adaptive::AdaptiveTree::Representation* rep(stream->m_adStream.getRepresentation());
  if (rep->flags_ & adaptive::AdaptiveTree::Representation::INCLUDEDSTREAM)
    m_IncludedStreams[stream->info_.GetStreamType()] = 0;
}

void CInputStreamAdaptive::EnableStream(int streamid, bool enable)
{
  LOG::Log(LOGDEBUG, "EnableStream(%d: %s)", streamid, enable ? "true" : "false");

  if (!m_session)
    return;

  Session::STREAM* stream(m_session->GetStream(streamid - m_session->GetPeriodId() * 1000));

  if (!enable && stream && stream->enabled)
  {
    UnlinkIncludedStreams(stream);
    m_session->EnableStream(stream, false);
  }
}

// We call true if a reset is required, otherwise false.
bool CInputStreamAdaptive::OpenStream(int streamid)
{
  LOG::Log(LOGDEBUG, "OpenStream(%d)", streamid);

  if (!m_session)
    return false;

  Session::STREAM* stream(m_session->GetStream(streamid - m_session->GetPeriodId() * 1000));

  if (!stream)
    return false;

  if (stream->enabled)
  {
    if (stream->m_adStream.StreamChanged())
    {
      UnlinkIncludedStreams(stream);
      stream->reset();
      stream->m_adStream.Reset();
    }
    else
      return false;
  }

  bool needRefetch = false; //Make sure that Kodi fetches changes
  stream->enabled = true;

  const adaptive::AdaptiveTree::Representation* rep(stream->m_adStream.getRepresentation());

  // If we select a dummy (=inside video) stream, open the video part
  // Dummy streams will be never enabled, they will only enable / activate audio track.
  if (rep->flags_ & adaptive::AdaptiveTree::Representation::INCLUDEDSTREAM)
  {
    Session::STREAM* mainStream;
    stream->mainId_ = 0;
    while ((mainStream = m_session->GetStream(++stream->mainId_)))
      if (mainStream->info_.GetStreamType() == INPUTSTREAM_TYPE_VIDEO && mainStream->enabled)
        break;
    if (mainStream)
    {
      ISampleReader* mainReader = mainStream->GetReader();
      if (!mainReader)
      {
        LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
      }
      else
      {
        mainReader->AddStreamType(stream->info_.GetStreamType(), streamid);
        mainReader->GetInformation(stream->info_);
      }
    }
    else
    {
      stream->mainId_ = 0;
    }
    m_IncludedStreams[stream->info_.GetStreamType()] = streamid;
    return false;
  }

  if (rep->flags_ & adaptive::AdaptiveTree::Representation::SUBTITLESTREAM)
  {
    stream->SetReader(std::make_unique<CSubtitleSampleReader>(
        rep->url_, streamid, stream->info_.GetCodecInternalName()));
    return false;
  }

  AP4_Movie* movie(m_session->PrepareStream(stream, needRefetch));

  // We load fragments on PrepareTime for HLS manifests and have to reevaluate the start-segment
  //if (m_session->GetManifestType() == PROPERTIES::ManifestType::HLS)
  //  stream->m_adStream.restart_stream();
  stream->m_adStream.start_stream();

  if (rep->containerType_ == adaptive::AdaptiveTree::CONTAINERTYPE_TEXT)
  {
    stream->SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream->m_adStream));
    stream->SetReader(std::make_unique<CSubtitleSampleReader>(stream, streamid,
                                                             stream->info_.GetCodecInternalName()));
  }
  else if (rep->containerType_ == adaptive::AdaptiveTree::CONTAINERTYPE_TS)
  {
    stream->SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream->m_adStream));

    uint32_t mask{(1U << stream->info_.GetStreamType()) | m_session->GetIncludedStreamMask()};
    stream->SetReader(std::make_unique<CTSSampleReader>(
        stream->GetAdByteStream(), stream->info_.GetStreamType(), streamid, mask));

    if (!stream->GetReader()->Initialize())
    {
      stream->disable();
      return false;
    }
    m_session->OnSegmentChanged(&stream->m_adStream);
  }
  else if (rep->containerType_ == adaptive::AdaptiveTree::CONTAINERTYPE_ADTS)
  {
    stream->SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream->m_adStream));
    stream->SetReader(std::make_unique<CADTSSampleReader>(stream->GetAdByteStream(), streamid));
  }
  else if (rep->containerType_ == adaptive::AdaptiveTree::CONTAINERTYPE_WEBM)
  {
    stream->SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream->m_adStream));
    stream->SetReader(std::make_unique<CWebmSampleReader>(stream->GetAdByteStream(), streamid));
    if (!stream->GetReader()->Initialize())
    {
      stream->disable();
      return false;
    }
  }
  else if (rep->containerType_ == adaptive::AdaptiveTree::CONTAINERTYPE_MP4)
  {
    stream->SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream->m_adStream));
    stream->SetStreamFile(std::make_unique<AP4_File>(
        *stream->GetAdByteStream(), AP4_DefaultAtomFactory::Instance_, true, movie));
    movie = stream->GetStreamFile()->GetMovie();

    if (movie == NULL)
    {
      LOG::Log(LOGERROR, "No MOOV in stream!");
      m_session->EnableStream(stream, false);
      return false;
    }

    AP4_Track* track = movie->GetTrack(TIDC[stream->m_adStream.get_type()]);
    if (!track)
    {
      if (stream->m_adStream.get_type() == adaptive::AdaptiveTree::SUBTITLE)
        track = movie->GetTrack(AP4_Track::TYPE_TEXT);
      if (!track)
      {
        LOG::Log(LOGERROR, "No suitable track found in stream");
        m_session->EnableStream(stream, false);
        return false;
      }
    }

    auto sampleDecrypter =
        m_session->GetSingleSampleDecryptor(stream->m_adStream.getRepresentation()->pssh_set_);
    auto caps = m_session->GetDecrypterCaps(stream->m_adStream.getRepresentation()->pssh_set_);

    stream->SetReader(std::make_unique<CFragmentedSampleReader>(
        stream->GetAdByteStream(), movie, track, streamid, sampleDecrypter, caps));
  }
  else
  {
    m_session->EnableStream(stream, false);
    return false;
  }

  if (stream->info_.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
  {
    for (uint16_t i(0); i < 16; ++i)
    {
      if (m_IncludedStreams[i])
      {
        stream->GetReader()->AddStreamType(static_cast<INPUTSTREAM_TYPE>(i), m_IncludedStreams[i]);
        unsigned int sid = m_IncludedStreams[i] - m_session->GetPeriodId() * 1000;
        Session::STREAM* incStream = m_session->GetStream(sid);
        if (!incStream) {
          LOG::LogF(LOGERROR, "Cannot get the stream from sid %u", sid);
        }
        stream->GetReader()->GetInformation(
            m_session->GetStream(m_IncludedStreams[i] - m_session->GetPeriodId() * 1000)->info_);
      }
    }
  }
  m_session->EnableStream(stream, true);
  return stream->GetReader()->GetInformation(stream->info_) || needRefetch;
}


DEMUX_PACKET* CInputStreamAdaptive::DemuxRead(void)
{
  if (!m_session)
    return NULL;

  if (m_checkChapterSeek)
  {
    m_checkChapterSeek = false;
    if (m_session->GetChapterSeekTime() > 0)
    {
      m_session->SeekTime(m_session->GetChapterSeekTime());
      m_session->ResetChapterSeekTime();
    }
  }

  if (~m_failedSeekTime)
  {
    LOG::Log(LOGDEBUG, "Seeking to last failed seek position (%d)", m_failedSeekTime);
    m_session->SeekTime(static_cast<double>(m_failedSeekTime) * 0.001f, 0, false);
    m_failedSeekTime = ~0;
  }

  ISampleReader* sr(m_session->GetNextSample());

  if (m_session->CheckChange())
  {
    DEMUX_PACKET* p = AllocateDemuxPacket(0);
    p->iStreamId = DEMUX_SPECIALID_STREAMCHANGE;
    LOG::Log(LOGDEBUG, "DEMUX_SPECIALID_STREAMCHANGE");
    return p;
  }

  if (sr)
  {
    AP4_Size iSize(sr->GetSampleDataSize());
    const AP4_UI08* pData(sr->GetSampleData());
    DEMUX_PACKET* p;

    if (sr->IsEncrypted() && iSize > 0 && pData)
    {
      unsigned int numSubSamples(*((unsigned int*)pData));
      pData += sizeof(numSubSamples);
      p = AllocateEncryptedDemuxPacket(iSize, numSubSamples);
      memcpy(p->cryptoInfo->clearBytes, pData, numSubSamples * sizeof(uint16_t));
      pData += (numSubSamples * sizeof(uint16_t));
      memcpy(p->cryptoInfo->cipherBytes, pData, numSubSamples * sizeof(uint32_t));
      pData += (numSubSamples * sizeof(uint32_t));
      memcpy(p->cryptoInfo->iv, pData, 16);
      pData += 16;
      memcpy(p->cryptoInfo->kid, pData, 16);
      pData += 16;
      iSize -= (pData - sr->GetSampleData());
      ReaderCryptoInfo cryptoInfo = sr->GetReaderCryptoInfo();
      p->cryptoInfo->flags = 0;
      p->cryptoInfo->cryptBlocks = cryptoInfo.m_cryptBlocks;
      p->cryptoInfo->skipBlocks = cryptoInfo.m_skipBlocks;
      p->cryptoInfo->mode = static_cast<uint16_t>(cryptoInfo.m_mode);
    }
    else
      p = AllocateDemuxPacket(iSize);

    if (iSize > 0 && pData)
    {
      p->dts = static_cast<double>(sr->DTS() + m_session->GetChapterStartTime());
      p->pts = static_cast<double>(sr->PTS() + m_session->GetChapterStartTime());
      p->duration = static_cast<double>(sr->GetDuration());
      p->iStreamId = sr->GetStreamId();
      p->iGroupId = 0;
      p->iSize = iSize;
      memcpy(p->pData, pData, iSize);
    }

    //LOG::Log(LOGDEBUG, "DTS: %0.4f, PTS:%0.4f, ID: %u SZ: %d", p->dts, p->pts, p->iStreamId, p->iSize);

    sr->ReadSample();
    return p;
  }

  if (m_session->SeekChapter(m_session->GetChapter() + 1))
  {
    m_checkChapterSeek = true;
    for (unsigned int i(1);
         i <= INPUTSTREAM_MAX_STREAM_COUNT && i <= m_session->GetStreamCount(); ++i)
      EnableStream(i + m_session->GetPeriodId() * 1000, false);
    m_session->InitializePeriod();
    DEMUX_PACKET* p = AllocateDemuxPacket(0);
    p->iStreamId = DEMUX_SPECIALID_STREAMCHANGE;
    LOG::Log(LOGDEBUG, "DEMUX_SPECIALID_STREAMCHANGE");
    return p;
  }
  return NULL;
}

// Accurate search (PTS based)
bool CInputStreamAdaptive::DemuxSeekTime(double time, bool backwards, double& startpts)
{
  return true;
}

//callback - will be called from kodi
void CInputStreamAdaptive::SetVideoResolution(int width, int height)
{
  LOG::Log(LOGINFO, "SetVideoResolution (%dx%d)", width, height);
  m_currentVideoWidth = width;
  m_currentVideoHeight = height;

  if (m_session)
    m_session->SetVideoResolution(width, height);
}

bool CInputStreamAdaptive::PosTime(int ms)
{
  if (!m_session)
    return false;

  LOG::Log(LOGINFO, "PosTime (%d)", ms);

  bool ret = m_session->SeekTime(static_cast<double>(ms) * 0.001f, 0, false);
  m_failedSeekTime = ret ? ~0 : ms;

  return ret;
}

int CInputStreamAdaptive::GetTotalTime()
{
  if (!m_session)
    return 0;

  return static_cast<int>(m_session->GetTotalTimeMs());
}

int CInputStreamAdaptive::GetTime()
{
  if (!m_session)
    return 0;

  int timeMs = static_cast<int>(m_session->GetElapsedTimeMs());
  return timeMs;
}

bool CInputStreamAdaptive::IsRealTimeStream()
{
  return m_session && m_session->IsLive();
}

#if INPUTSTREAM_VERSION_LEVEL > 1
int CInputStreamAdaptive::GetChapter()
{
  return m_session ? m_session->GetChapter() : 0;
}

int CInputStreamAdaptive::GetChapterCount()
{
  return m_session ? m_session->GetChapterCount() : 0;
}

const char* CInputStreamAdaptive::GetChapterName(int ch)
{
  return m_session ? m_session->GetChapterName(ch) : 0;
}

int64_t CInputStreamAdaptive::GetChapterPos(int ch)
{
  return m_session ? m_session->GetChapterPos(ch) : 0;
}

bool CInputStreamAdaptive::SeekChapter(int ch)
{
  return m_session ? m_session->SeekChapter(ch) : false;
}
#endif
/*****************************************************************************************************/

CVideoCodecAdaptive::CVideoCodecAdaptive(const kodi::addon::IInstanceInfo& instance)
  : CInstanceVideoCodec(instance),
    m_session(nullptr),
    m_state(0),
    m_name("inputstream.adaptive.decoder")
{
}

CVideoCodecAdaptive::CVideoCodecAdaptive(const kodi::addon::IInstanceInfo& instance,
                                         CInputStreamAdaptive* parent)
  : CInstanceVideoCodec(instance), m_session(parent->GetSession()), m_state(0)
{
}

CVideoCodecAdaptive::~CVideoCodecAdaptive()
{
}

bool CVideoCodecAdaptive::Open(const kodi::addon::VideoCodecInitdata& initData)
{
  if (!m_session || !m_session->GetDecrypter())
    return false;

  if (initData.GetCodecType() == VIDEOCODEC_H264 && !initData.GetExtraDataSize() &&
      !(m_state & STATE_WAIT_EXTRADATA))
  {
    LOG::Log(LOGINFO, "VideoCodec::Open: Wait ExtraData");
    m_state |= STATE_WAIT_EXTRADATA;
    return true;
  }
  m_state &= ~STATE_WAIT_EXTRADATA;

  LOG::Log(LOGINFO, "VideoCodec::Open");

  m_name = "inputstream.adaptive";
  switch (initData.GetCodecType())
  {
    case VIDEOCODEC_VP8:
      m_name += ".vp8";
      break;
    case VIDEOCODEC_H264:
      m_name += ".h264";
      break;
    case VIDEOCODEC_VP9:
      m_name += ".vp9";
      break;
    default:;
  }
  m_name += ".decoder";

  std::string sessionId(initData.GetCryptoSession().GetSessionId());
  Adaptive_CencSingleSampleDecrypter* ssd(m_session->GetSingleSampleDecrypter(sessionId));

  return m_session->GetDecrypter()->OpenVideoDecoder(
      ssd, reinterpret_cast<const SSD::SSD_VIDEOINITDATA*>(initData.GetCStructure()));
}

bool CVideoCodecAdaptive::Reconfigure(const kodi::addon::VideoCodecInitdata& initData)
{
  return false;
}

bool CVideoCodecAdaptive::AddData(const DEMUX_PACKET& packet)
{
  if (!m_session || !m_session->GetDecrypter())
    return false;

  SSD::SSD_SAMPLE sample;
  sample.data = packet.pData;
  sample.dataSize = packet.iSize;
  sample.flags = 0;
  sample.pts = (int64_t)packet.pts;
  if (packet.cryptoInfo)
  {
    sample.numSubSamples = packet.cryptoInfo->numSubSamples;
    sample.clearBytes = packet.cryptoInfo->clearBytes;
    sample.cipherBytes = packet.cryptoInfo->cipherBytes;
    sample.iv = packet.cryptoInfo->iv;
    sample.kid = packet.cryptoInfo->kid;
  }
  else
  {
    sample.numSubSamples = 0;
    sample.iv = sample.kid = nullptr;
  }

  return m_session->GetDecrypter()->DecodeVideo(
             dynamic_cast<kodi::addon::CInstanceVideoCodec*>(this), &sample, nullptr) !=
         SSD::VC_ERROR;
}

VIDEOCODEC_RETVAL CVideoCodecAdaptive::GetPicture(VIDEOCODEC_PICTURE& picture)
{
  if (!m_session || !m_session->GetDecrypter())
    return VIDEOCODEC_RETVAL::VC_ERROR;

  static VIDEOCODEC_RETVAL vrvm[] = {VIDEOCODEC_RETVAL::VC_NONE, VIDEOCODEC_RETVAL::VC_ERROR,
                                     VIDEOCODEC_RETVAL::VC_BUFFER, VIDEOCODEC_RETVAL::VC_PICTURE,
                                     VIDEOCODEC_RETVAL::VC_EOF};

  return vrvm[m_session->GetDecrypter()->DecodeVideo(
      dynamic_cast<kodi::addon::CInstanceVideoCodec*>(this), nullptr,
      reinterpret_cast<SSD::SSD_PICTURE*>(&picture))];
}

void CVideoCodecAdaptive::Reset()
{
  if (!m_session || !m_session->GetDecrypter())
    return;

  m_session->GetDecrypter()->ResetVideo();
}

/*****************************************************************************************************/

class ATTR_DLL_LOCAL CMyAddon : public kodi::addon::CAddonBase
{
public:
  CMyAddon();
  virtual ~CMyAddon();
  ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo& instance,
                              KODI_ADDON_INSTANCE_HDL& hdl) override;
};

CMyAddon::CMyAddon()
{
  kodihost = nullptr;
  ;
}

CMyAddon::~CMyAddon()
{
  delete kodihost;
}

ADDON_STATUS CMyAddon::CreateInstance(const kodi::addon::IInstanceInfo& instance,
                                      KODI_ADDON_INSTANCE_HDL& hdl)
{
  if (instance.IsType(ADDON_INSTANCE_INPUTSTREAM))
  {
    hdl = new CInputStreamAdaptive(instance);
    kodihost = new KodiHost();
    return ADDON_STATUS_OK;
  }
  return ADDON_STATUS_NOT_IMPLEMENTED;
}

ADDONCREATOR(CMyAddon);
