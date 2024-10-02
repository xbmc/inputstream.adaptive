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

// Chooser's properties that will override XML settings
struct ChooserProps
{
  std::string m_chooserType; // Specifies chooser type to be used
  uint32_t m_bandwidthMax{0};
  std::pair<int, int> m_resolutionMax; // Res. limit for non-protected videos (values 0 means auto)
  std::pair<int, int> m_resolutionSecureMax; // Res. limit for DRM protected videos (values 0 means auto)
};

// Generic add-on configuration
struct Config
{
  // Determines whether curl verifies the authenticity of the peer's certificate,
  // if set to false CA certificates are not loaded and verification will be skipped.
  bool curlSSLVerifyPeer{true};
  // Determines if cookies are internally handled by InputStream Adaptive add-on
  bool internalCookies{false};
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
  // Custom delay from LIVE edge in seconds
  uint64_t liveDelay{0};
};

struct DrmCfg
{
  // Priority over other DRM configurations, a value of 0 means unset
  std::optional<uint32_t> priority;
  // Custom init data encoded as base64 to make the CDM initialization
  std::string initData;
  // Pre-init data encoded as base64 to make pre-initialization of Widevine CDM
  // The data is represented as a string of base64 data splitted by a pipe char
  // "{PSSH as base64}|{KID as base64}"
  std::string preInitData;
  // To enable persistent state CDM behaviour
  bool isPersistentStorage{false};
  // To force enable/disable the secure decoder (it could be overrided)
  std::optional<bool> isSecureDecoderEnabled;
  // Optional parameters to make the CDM key request (CDM specific parameters)
  std::map<std::string, std::string> optKeyReqParams;

  struct License
  {
    // The license server certificate encoded as base64
    std::string serverCert;
    // The license server url
    std::string serverUrl;
    // To force an HTTP GET request, instead that POST request
    bool isHttpGetRequest{false};
    // HTTP request headers
    std::map<std::string, std::string> reqHeaders;
    // HTTP parameters to append to the url
    std::string reqParams;
    // Custom license data encoded as base64 to make the HTTP license request
    std::string reqData;
    // License data wrappers
    // Multiple wrappers supported e.g. "base64,json", the name order defines the order
    // in which data will be wrapped, (1) base64 --> (2) url
    std::string wrapper;
    // License data unwrappers
    // Multiple un-wrappers supported e.g. "base64,json", the name order defines the order
    // in which data will be unwrapped, (1) base64 --> (2) json
    std::string unwrapper;
    // License data unwrappers parameters
    std::map<std::string, std::string> unwrapperParams;
    // Clear key's for ClearKey DRM (KID / KEY pair)
    std::map<std::string, std::string> keys;
  };

  // The license configuration
  License license;
  // Specifies if has been parsed the new DRM config ("drm" or "drm_legacy" kodi property)
  //! @todo: to remove when deprecated DRM properties will be removed
  bool isNewConfig{true};
};

class ATTR_DLL_LOCAL CCompKodiProps
{
public:
  CCompKodiProps() = default;
  ~CCompKodiProps() = default;

  void Init(const std::map<std::string, std::string>& props);

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

  // \brief Specifies the chooser properties that will override XML settings
  const ChooserProps& GetChooserProps() const { return m_chooserProps; }

  // \brief Specifies generic add-on configuration
  const Config& GetConfig() const { return m_config; }

  // \brief Specifies the manifest configuration
  const ManifestConfig& GetManifestConfig() const { return m_manifestConfig; }


  //! @todo: temporary method, for future rework
  const std::string GetDrmKeySystem()
  {
    return m_drmConfigs.empty() ? "" : m_drmConfigs.begin()->first;
  }
  //! @todo: temporary method, for future rework
  const DrmCfg& GetDrmConfig() { return m_drmConfigs[GetDrmKeySystem()]; }


  // \brief Get DRM configuration for specified keysystem, if not found will return default values
  const DrmCfg& GetDrmConfig(const std::string& keySystem) { return m_drmConfigs[keySystem]; }
  const DrmCfg& GetDrmConfig(std::string_view keySystem) { return m_drmConfigs[std::string(keySystem)]; }

  const std::map<std::string, DrmCfg>& GetDrmConfigs() const { return m_drmConfigs; }

private:
  void ParseConfig(const std::string& data);
  void ParseManifestConfig(const std::string& data);

  void ParseDrmOldProps(const std::map<std::string, std::string>& props);
  bool ParseDrmConfig(const std::string& data);
  bool ParseDrmLegacyConfig(const std::string& data);

  std::string m_manifestUpdParams;
  std::string m_manifestParams;
  std::map<std::string, std::string> m_manifestHeaders;
  std::string m_streamParams;
  std::map<std::string, std::string> m_streamHeaders;
  std::string m_audioLanguageOrig;
  bool m_playTimeshiftBuffer{false};
  ChooserProps m_chooserProps;
  Config m_config;
  ManifestConfig m_manifestConfig;
  std::map<std::string, DrmCfg> m_drmConfigs;
};

} // namespace KODI_PROPS
} // namespace ADP
