/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ADP
{
namespace KODI_PROPS
{

enum class ManifestType // Deprecated
{
  UNKNOWN = 0,
  MPD,
  ISM,
  HLS
};

// Chooser's properties that will override XML settings
struct ChooserProps
{
  std::string m_chooserType; // Specifies chooser type to be used
  uint32_t m_bandwidthMax{0};
  std::pair<int, int> m_resolutionMax; // Res. limit for non-protected videos (values 0 means auto)
  std::pair<int, int> m_resolutionSecureMax; // Res. limit for DRM protected videos (values 0 means auto)
};

struct ManifestConfig
{
  // Limit the timeshift buffer depth, in seconds
  std::optional<uint32_t> timeShiftBufferLimit;
  // Faulty HLS live services can send manifest updates with inconsistent EXT-X-ENDLIST
  // when the stream is not finished, enabling this will ignore EXT-X-ENDLIST tags
  bool hlsIgnoreEndList{false};
  // Faulty HLS live services can send manifest updates with inconsistent EXT-X-MEDIA-SEQUENCE
  // enabling this will correct the value by using EXT-X-PROGRAM-DATE-TIME tags
  bool hlsFixMediaSequence{false};
  // Faulty HLS live services can send manifest updates with inconsistent EXT-X-DISCONTINUITY-SEQUENCE
  // enabling this will correct the value by using EXT-X-PROGRAM-DATE-TIME tags
  bool hlsFixDiscontSequence{false};
};

struct DrmCfg
{
  struct License
  {
    // Multiple wrappers e.g. "base64+json", the name order defines the order
    // in which data will be unwrapped, (1) base64 --> (2) json
    std::string m_wrapper;
    std::map<std::string, std::string> m_wrapperParams;

    //! @todo: To be removed at same time of deprecated DRM properties, this is an old hack used to traverse all
    //! JSON objects to find a specific key name in a dict (the new implementation use absolute JSON paths)
    bool m_isJsonPathTraverse{false}; // todo<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

    std::string m_serverCertificate; // Encoded as base64, same of inputstream.adaptive.server_certificate

    std::string m_serverUrl;
    std::map<std::string, std::string> m_reqHeaders; // todo: make it optional, if not set get headers from manifest  <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    std::string m_reqParams;
    std::string m_reqData; // Encoded as base64, if set will be executed an HTTP POST request with provided data
  };

  License m_license; // The license configuration

  bool m_isPersistentStorage{false}; // same of inputstream.adaptive.license_flags
  bool m_isSecureDecoderForced{false}; // same of inputstream.adaptive.license_flags
  
  std::string m_streamsPsshData; // Encoded as base64, same of inputstream.adaptive.license_data
  std::string m_preInitData; // Encoded as base64, same of 

  // todo <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
  std::optional<int> m_priority;
};

class ATTR_DLL_LOCAL CCompKodiProps
{
public:
  CCompKodiProps(const std::map<std::string, std::string>& props);
  ~CCompKodiProps() = default;

  std::string_view GetLicenseType() const { return m_licenseType; }
  std::string_view GetLicenseKey() const { return m_licenseKey; }
  // \brief Get custom PSSH initialization license data
  std::string_view GetLicenseData() const { return m_licenseData; }

  bool IsLicensePersistentStorage() const { return m_isLicensePersistentStorage; }
  bool IsLicenseForceSecDecoder() const { return m_isLicenseForceSecureDecoder; }

  std::string_view GetServerCertificate() const { return m_serverCertificate; }
  ManifestType GetManifestType() const { return m_manifestType; } // Deprecated

  std::string GetManifestUpdParam() const { return m_manifestUpdateParam; } // Deprecated
  // \brief HTTP parameters used to download manifest updates
  std::string GetManifestUpdParams() const { return m_manifestUpdParams; }
  // \brief HTTP parameters used to download manifests
  std::string GetManifestParams() const { return m_manifestParams; }
  // \brief HTTP headers used to download manifest
  std::map<std::string, std::string> GetManifestHeaders() const { return m_manifestHeaders; }

  // \brief HTTP parameters used to download streams
  std::string GetStreamParams() const { return m_streamParams; }
  // \brief HTTP headers used to download streams
  std::map<std::string, std::string> GetStreamHeaders() const { return m_streamHeaders; }

  // \brief Get language code to identify the audio track in original language
  std::string GetAudioLangOrig() const { return m_audioLanguageOrig; }

  // \brief Specify to start playing a LIVE stream from the beginning of the buffer instead of its end
  bool IsPlayTimeshift() const { return m_playTimeshiftBuffer; }
  // \brief Get a custom delay from LIVE edge in seconds
  uint64_t GetLiveDelay() const { return m_liveDelay; }

  /*
   * \brief Get data to "pre-initialize" the DRM, if set is represented as a string
   *        of base64 data splitted by a pipe char as: "{PSSH as base64}|{KID as base64}".
   */
  std::string_view GetDrmPreInitData() const { return m_drmPreInitData; }

  // \brief Defines if cookies are internally handled by InputStream Adaptive add-on
  bool IsInternalCookies() const { return m_isInternalCookies; }

  // \brief Specifies the chooser properties that will override XML settings
  const ChooserProps& GetChooserProps() const { return m_chooserProps; }

  // \brief Specifies the manifest configuration
  const ManifestConfig& GetManifestConfig() const { return m_manifestConfig; }

  // \brief Get DRM configuration for specified keysystem, if not found will return default values
  const DrmCfg& GetDrmConfig(const std::string& keySystem) { return m_drmConfigs[keySystem]; }

  const std::map<std::string, DrmCfg>& GetDrmConfigs() const { return m_drmConfigs; }

private:
  void ParseManifestConfig(const std::string& data);
  void ParseLegacyDrm(const std::map<std::string, std::string>& props);
  bool ParseDrm(std::string data);
  bool ParseDrmLicense(std::string data);

  std::string m_licenseType;
  std::string m_licenseKey;
  std::string m_licenseData;
  bool m_isLicensePersistentStorage{false};
  bool m_isLicenseForceSecureDecoder{false};
  std::string m_serverCertificate;
  ManifestType m_manifestType{ManifestType::UNKNOWN}; // Deprecated
  std::string m_manifestUpdateParam; // Deprecated
  std::string m_manifestUpdParams;
  std::string m_manifestParams;
  std::map<std::string, std::string> m_manifestHeaders;
  std::string m_streamParams;
  std::map<std::string, std::string> m_streamHeaders;
  std::string m_audioLanguageOrig;
  bool m_playTimeshiftBuffer{false};
  uint64_t m_liveDelay{0};
  std::string m_drmPreInitData;
  bool m_isInternalCookies{false};
  ChooserProps m_chooserProps;
  ManifestConfig m_manifestConfig;
  // DRM configurations by CDM key system
  std::map<std::string, DrmCfg> m_drmConfigs;
};

} // namespace KODI_PROPS
} // namespace ADP
