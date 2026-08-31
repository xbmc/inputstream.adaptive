/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CompKodiProps.h"

#include "CompSettings.h"
#include "SrvBroker.h"
#include "decrypters/Helpers.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <nlohmann/json.hpp>

#include <string_view>

using njson = nlohmann::json;
using namespace UTILS;
using namespace ADP::KODI_PROPS;

namespace
{
// clang-format off
constexpr std::string_view PROP_LICENSE_TYPE = "inputstream.adaptive.license_type"; //! @todo: to be removed on Kodi 24
constexpr std::string_view PROP_LICENSE_KEY = "inputstream.adaptive.license_key"; //! @todo: to be removed on Kodi 24

constexpr std::string_view PROP_COMMON_HEADERS = "inputstream.adaptive.common_headers";

constexpr std::string_view PROP_MANIFEST_PARAMS = "inputstream.adaptive.manifest_params";
constexpr std::string_view PROP_MANIFEST_HEADERS = "inputstream.adaptive.manifest_headers";
constexpr std::string_view PROP_MANIFEST_UPD_PARAMS = "inputstream.adaptive.manifest_upd_params";
constexpr std::string_view PROP_MANIFEST_CONFIG = "inputstream.adaptive.manifest_config";

constexpr std::string_view PROP_STREAM_PARAMS = "inputstream.adaptive.stream_params";
constexpr std::string_view PROP_STREAM_HEADERS = "inputstream.adaptive.stream_headers";

constexpr std::string_view PROP_PLAY_TIMESHIFT_BUFFER = "inputstream.adaptive.play_timeshift_buffer";

constexpr std::string_view PROP_CONFIG = "inputstream.adaptive.config";
constexpr std::string_view PROP_DRM = "inputstream.adaptive.drm";
constexpr std::string_view PROP_DRM_LEGACY = "inputstream.adaptive.drm_legacy";

// Chooser's properties
constexpr std::string_view PROP_STREAM_SELECTION_TYPE = "inputstream.adaptive.stream_selection_type";
constexpr std::string_view PROP_CHOOSER_BANDWIDTH_MAX = "inputstream.adaptive.chooser_bandwidth_max";
constexpr std::string_view PROP_CHOOSER_RES_MAX = "inputstream.adaptive.chooser_resolution_max";
constexpr std::string_view PROP_CHOOSER_RES_SECURE_MAX = "inputstream.adaptive.chooser_resolution_secure_max";
// clang-format on


void LogProp(std::string_view name, std::string_view value, bool isValueRedacted = false)
{
  LOG::Log(LOGDEBUG, "Property found \"%s\" value: %s", name.data(),
           isValueRedacted ? "[redacted]" : value.data());
}

void LogDrmJsonDictKeys(std::string_view keyName,
                        const njson& dictValue,
                        std::string_view keySystem)
{
  if (dictValue.is_object())
  {
    std::string keys;
    for (auto& [name, jValue] : dictValue.items())
    {
      if (!keys.empty())
        keys += ", ";
      keys += name;
    }
    LOG::Log(LOGDEBUG,
             "Found DRM config for key system: \"%s\" -> Dictionary: \"%s\", Values: \"%s\"",
             keySystem.data(), keyName.data(), keys.c_str());
  }
}
} // unnamed namespace

void ADP::KODI_PROPS::CCompKodiProps::InitStage1(const std::map<std::string, std::string>& props)
{
  std::string licenseUrl;

  if (STRING::KeyExists(props, PROP_LICENSE_TYPE) || STRING::KeyExists(props, PROP_LICENSE_KEY)) //! @todo: to be removed on Kodi 24
  {
    LOG::Log(LOGERROR,
             "<<<<<<<<< WRONG DRM CONFIGURATION >>>>>>>>>\n"
             "DRM WAS CONFIGURED USING DEPRECATED PROPERTIES THAT ARE NO LONGER SUPPORTED.\n"
             "THE FOLLOWING PROPERTIES ARE NO LONGER SUPPORTED:\n"
             "- inputstream.adaptive.license_type\n"
             "- inputstream.adaptive.license_key\n"
             "- inputstream.adaptive.license_data\n"
             "- inputstream.adaptive.license_flags\n"
             "- inputstream.adaptive.server_certificate\n"
             "- inputstream.adaptive.pre_init_data\n"
             "YOU MUST MIGRATE TO THE NEW PROPERTIES:\n"
             "- inputstream.adaptive.drm_legacy\n"
             "- inputstream.adaptive.drm\n"
             "FOR MORE INFO, PLEASE READ THE WIKI PAGE: "
             "https://github.com/xbmc/inputstream.adaptive/wiki/Integration-DRM");
    return;
  }

  for (const auto& prop : props)
  {
    const bool isRedacted = !CSrvBroker::GetSettings().IsDebugVerbose();

    if (prop.first == PROP_COMMON_HEADERS)
    {
      LogProp(prop.first, prop.second);
      ParseHeaderString(m_commonHeaders, prop.second);
    }
    else if (prop.first == PROP_MANIFEST_UPD_PARAMS)
    {
      LogProp(prop.first, prop.second);
      m_manifestUpdParams = prop.second;
    }
    else if (prop.first == PROP_MANIFEST_PARAMS)
    {
      LogProp(prop.first, prop.second);
      m_manifestParams = prop.second;
    }
    else if (prop.first == PROP_MANIFEST_HEADERS)
    {
      LogProp(prop.first, prop.second);
      ParseHeaderString(m_manifestHeaders, prop.second);
    }
    else if (prop.first == PROP_STREAM_PARAMS)
    {
      LogProp(prop.first, prop.second);
      m_streamParams = prop.second;
    }
    else if (prop.first == PROP_STREAM_HEADERS)
    {
      LogProp(prop.first, prop.second);
      ParseHeaderString(m_streamHeaders, prop.second);
    }
    else if (prop.first == PROP_PLAY_TIMESHIFT_BUFFER)
    {
      LogProp(prop.first, prop.second);
      m_playTimeshiftBuffer = STRING::CompareNoCase(prop.second, "true");
    }
    else if (prop.first == PROP_STREAM_SELECTION_TYPE)
    {
      LogProp(prop.first, prop.second);
      m_chooserProps.m_chooserType = prop.second;
    }
    else if (prop.first == PROP_CHOOSER_BANDWIDTH_MAX)
    {
      LogProp(prop.first, prop.second);
      m_chooserProps.m_bandwidthMax = static_cast<uint32_t>(std::stoi(prop.second));
    }
    else if (prop.first == PROP_CHOOSER_RES_MAX)
    {
      LogProp(prop.first, prop.second);
      std::pair<int, int> res;
      if (STRING::GetMapValue(ADP::SETTINGS::RES_CONV_LIST, prop.second, res))
        m_chooserProps.m_resolutionMax = res;
      else
        LOG::Log(LOGERROR, "Resolution not valid on \"%s\" property.", prop.first.c_str());
    }
    else if (prop.first == PROP_CHOOSER_RES_SECURE_MAX)
    {
      LogProp(prop.first, prop.second);
      std::pair<int, int> res;
      if (STRING::GetMapValue(ADP::SETTINGS::RES_CONV_LIST, prop.second, res))
        m_chooserProps.m_resolutionSecureMax = res;
      else
        LOG::Log(LOGERROR, "Resolution not valid on \"%s\" property.", prop.first.c_str());
    }
    else if (prop.first == PROP_CONFIG)
    {
      LogProp(prop.first, prop.second);
      ParseConfig(prop.second);
    }
    else if (prop.first == PROP_MANIFEST_CONFIG)
    {
      LogProp(prop.first, prop.second);
      ParseManifestConfig(prop.second);
    }
    else if (prop.first == PROP_DRM && !prop.second.empty())
    {
      LogProp(prop.first, prop.second, isRedacted);
      if (!ParseDrmConfig(prop.second))
        LOG::LogF(LOGERROR, "Cannot parse \"%s\" property, wrong or malformed data.",
          prop.first.c_str());
    }
    else if (prop.first == PROP_DRM_LEGACY && !prop.second.empty())
    {
      LogProp(prop.first, prop.second, isRedacted);
      if (!ParseDrmLegacyConfig(prop.second))
        LOG::LogF(LOGERROR, "Cannot parse \"%s\" property, wrong or malformed data.",
                  prop.first.c_str());
    }
    else
    {
      LOG::Log(LOGWARNING, "Property found \"%s\" is not supported", prop.first.c_str());
      continue;
    }
  }
}

bool ADP::KODI_PROPS::CCompKodiProps::HasDrmConfig(std::string_view keySystem) const
{
  return STRING::KeyExists(m_drmConfigs, keySystem);
}

const ADP::KODI_PROPS::DrmCfg ADP::KODI_PROPS::CCompKodiProps::GetDrmConfig(
    std::string_view keySystem) const
{
  if (STRING::KeyExists(m_drmConfigs, keySystem))
    return m_drmConfigs.at(keySystem.data());

  return {}; // default values
}

void ADP::KODI_PROPS::CCompKodiProps::ParseConfig(const std::string& data)
{
  /*
   * Expected JSON structure:
   * { "config_name": "value", ... }
   */
  const njson jData = njson::parse(data, nullptr, false);
  if (jData.is_discarded() || !jData.is_object())
  {
    LOG::LogF(LOGERROR, "Malformed JSON data in to \"%s\" property", PROP_CONFIG.data());
    return;
  }

  // Iterate dictionary
  for (auto& [configName, jValue] : jData.items())
  {
    if (configName == "ssl_verify_peer" && jValue.is_boolean())
    {
      m_config.curlSSLVerifyPeer = jValue.get<bool>();
    }
    else if (configName == "disable_accept_encoding" && jValue.is_boolean())
    {
      m_config.curlDisableAcceptEncoding = jValue.get<bool>();
    }
    else if (configName == "internal_cookies" && jValue.is_boolean())
    {
      m_config.internalCookies = jValue.get<bool>();
    }
    else if (configName == "check_hdcp" && jValue.is_string())
    {
      std::string_view value = jValue.get<std::string_view>();

      if (value.empty() || value == "default")
        m_config.hdcpCheck = HdcpCheckType::DEFAULT;
      else if (value == "license")
        m_config.hdcpCheck = HdcpCheckType::LICENSE;
      else
        LOG::LogF(LOGERROR, "Value \"%s\" isnt supported on \"%s\" config of \"%s\" property",
                  value.data(), configName.c_str(), PROP_MANIFEST_CONFIG.data());
    }
    else if (configName == "resolution_limit" && jValue.is_string())
    {
      std::string_view value = jValue.get<std::string_view>();
      if (!value.empty())
      {
        auto pos = value.find('x');
        if (pos != std::string_view::npos)
        {
          const int width = STRING::ToInt32(value.substr(0, pos));
          const int height = STRING::ToInt32(value.substr(pos + 1));
          m_config.resolutionLimit = width * height;
        }
        else
        {
          LOG::LogF(LOGERROR,
                    "Invalid resolution format \"%s\" on \"%s\" config of \"%s\" property",
                    value.data(), configName.c_str(), PROP_MANIFEST_CONFIG.data());
        }
      }
    }
    else if (configName == "media_audio_langcode_default" && jValue.is_string())
    {
      m_config.mediaAudioLangCodeDef = jValue.get<std::string>();
    }
    else if (configName == "media_audio_langcode_original" && jValue.is_string())
    {
      m_config.mediaAudioLangCodeOrig = jValue.get<std::string>();
    }
    else if (configName == "media_subtitle_langcode_default" && jValue.is_string())
    {
      m_config.mediaSubtitleLangCodeDef = jValue.get<std::string>();
    }
    else if (configName == "media_audio_type_pref" && jValue.is_string())
    {
      // Accepted values mimic Kodi VP language settings: "original", "impaired", "default"
      if (jValue == "" || jValue == "default")
        m_config.mediaAudioTypePref = MediaFlagType::DEFAULT;
      else if (jValue == "original")
        m_config.mediaAudioTypePref = MediaFlagType::ORIGINAL;
      else if (jValue == "impaired")
        m_config.mediaAudioTypePref = MediaFlagType::IMPAIRED;
      else
        LOG::LogF(LOGERROR, "Value \"%s\" isnt supported on \"%s\" parameter of \"%s\" property",
                  jValue.get<std::string>().c_str(), configName.c_str(),
                  PROP_MANIFEST_CONFIG.data());
    }
    else if (configName == "media_audio_stereo_pref" && jValue.is_boolean())
    {
      m_config.mediaAudioStereoPref = jValue.get<bool>();
    }
    else
    {
      LOG::LogF(LOGERROR, "Unsupported \"%s\" config or wrong data type on \"%s\" property",
                configName.c_str(), PROP_MANIFEST_CONFIG.data());
    }
  }
}

void ADP::KODI_PROPS::CCompKodiProps::ParseManifestConfig(const std::string& data)
{
  /*
   * Expected JSON structure:
   * { "config_name": "value", ... }
   */
  const njson jData = njson::parse(data, nullptr, false);
  if (jData.is_discarded() || !jData.is_object())
  {
    LOG::LogF(LOGERROR, "Malformed JSON data in to \"%s\" property", PROP_MANIFEST_CONFIG.data());
    return;
  }

  // Iterate dictionary
  for (auto& [configName, jValue] : jData.items())
  {
    if (configName == "timeshift_bufferlimit" && jValue.is_number_unsigned())
    {
      if (jValue.get<uint32_t>() > 0)
        m_manifestConfig.timeShiftBufferLimit = jValue.get<uint32_t>();
    }
    else if (configName == "hls_ignore_endlist" && jValue.is_boolean())
    {
      m_manifestConfig.hlsIgnoreEndList = jValue.get<bool>();
    }
    else if (configName == "hls_fix_mediasequence" && jValue.is_boolean())
    {
      m_manifestConfig.hlsFixMediaSequence = jValue.get<bool>();
    }
    else if (configName == "hls_fix_discsequence" && jValue.is_boolean())
    {
      m_manifestConfig.hlsFixDiscontSequence = jValue.get<bool>();
    }
    else if (configName == "live_delay" && jValue.is_number_unsigned())
    {
      m_manifestConfig.liveDelay = jValue.get<uint64_t>();
    }
    else if (configName == "dash_utctiming" && jValue.is_object())
    {
      for (auto& [schemeId, jValue] : jValue.items()) // Iterate JSON dict
      {
        if (!jValue.is_string())
        {
          LOG::LogF(LOGERROR, "The manifest parameter \"dash_utctiming\" contains invalid values");
          break;
        }
        std::pair<std::string, std::string> utcTiming;
        utcTiming.first = schemeId;
        utcTiming.second = jValue.get<std::string>();
        m_manifestConfig.dashUTCTiming = utcTiming;
        break;
      }
    }
    else if (configName == "ignore_media_defaultkid" && jValue.is_boolean())
    {
      m_manifestConfig.ignoreMediaDefaultKid = jValue.get<bool>();
    }
    else
    {
      LOG::LogF(LOGERROR, "Unsupported \"%s\" config or wrong data type on \"%s\" property",
                configName.c_str(), PROP_MANIFEST_CONFIG.data());
    }
  }
}

bool ADP::KODI_PROPS::CCompKodiProps::ParseDrmConfig(const std::string& data)
{
  /* Expected JSON structure:
   * { "keysystem_name" : { "persistent_storage" : bool,
   *                        "init_data" : str,
   *                        "pre_init_data" : str,
   *                        "priority": int, 
   *                        "license": dict,
   *                        ... },
   *   "keysystem_name_2" : { ... }}
   */
  const njson jData = njson::parse(data, nullptr, false);
  if (jData.is_discarded() || !jData.is_object())
  {
    LOG::LogF(LOGERROR, "Malformed JSON data in to \"%s\" property", PROP_DRM.data());
    return false;
  }

  // Iterate key systems dict
  for (auto& [keySystem, jValue] : jData.items())
  {
    if (!DRM::IsValidKeySystem(keySystem))
    {
      LOG::LogF(LOGERROR, "Ignored unknown key system \"%s\" on DRM property", keySystem.c_str());
      continue;
    }

    DrmCfg& drmCfg = m_drmConfigs[keySystem]; // create new configuration

    if (!jValue.is_object())
    {
      LOG::LogF(LOGERROR, "Cannot parse key system \"%s\" value on DRM property, wrong data type",
                keySystem.c_str());
      continue;
    }

    // Parse main DRM config

    LogDrmJsonDictKeys("main", jValue, keySystem);

    if (jValue.contains("force_single_session") && jValue["force_single_session"].is_boolean())
      drmCfg.isForceSingleSession = jValue["force_single_session"].get<bool>();

    if (jValue.contains("persistent_storage") && jValue["persistent_storage"].is_boolean())
      drmCfg.isPersistentStorage = jValue["persistent_storage"].get<bool>();

    if (jValue.contains("secure_decoder") && jValue["secure_decoder"].is_boolean())
      drmCfg.isSecureDecoderEnabled = jValue["secure_decoder"].get<bool>();

    if (jValue.contains("init_data") && jValue["init_data"].is_string())
      drmCfg.initData = jValue["init_data"].get<std::string>();

    if (jValue.contains("pre_init_data") && jValue["pre_init_data"].is_string())
      drmCfg.preInitData = jValue["pre_init_data"].get<std::string>();

    if (jValue.contains("optional_key_req_params") && jValue["optional_key_req_params"].is_object())
    {
      for (auto& [paramName, jValue] : jData.items()) // Iterate JSON dict
      {
        if (!jValue.is_string())
        {
          LOG::LogF(LOGERROR, "The DRM parameter \"optional_key_req_params\" contains invalid values");
          break;
        }
        drmCfg.optKeyReqParams.emplace(paramName, jValue.get<std::string>());
      }
    }

    if (jValue.contains("priority") && jValue["priority"].is_number_unsigned())
      drmCfg.priority = jValue["priority"].get<uint32_t>();

    // Parse license DRM config

    if (jValue.contains("license") && jValue["license"].is_object())
    {
      auto& jDictLic = jValue["license"];

      LogDrmJsonDictKeys("license", jDictLic, keySystem);

      if (jDictLic.contains("server_certificate") && jDictLic["server_certificate"].is_string())
        drmCfg.license.serverCert = jDictLic["server_certificate"].get<std::string>();

      if (jDictLic.contains("server_url") && jDictLic["server_url"].is_string())
        drmCfg.license.serverUri = jDictLic["server_url"].get<std::string>();

      if (jDictLic.contains("use_http_get_request") && jDictLic["use_http_get_request"].is_boolean())
        drmCfg.license.isHttpGetRequest = jDictLic["use_http_get_request"].get<bool>();

      if (jDictLic.contains("req_headers") && jDictLic["req_headers"].is_string())
        ParseHeaderString(drmCfg.license.reqHeaders, jDictLic["req_headers"].get<std::string>());

      if (jDictLic.contains("req_params") && jDictLic["req_params"].is_string())
        drmCfg.license.reqParams = jDictLic["req_params"].get<std::string>();

      if (jDictLic.contains("req_data") && jDictLic["req_data"].is_string())
        drmCfg.license.reqData = jDictLic["req_data"].get<std::string>();

      if (jDictLic.contains("wrapper") && jDictLic["wrapper"].is_string())
        drmCfg.license.wrapper = STRING::ToLower(jDictLic["wrapper"].get<std::string>());

      if (jDictLic.contains("unwrapper") && jDictLic["unwrapper"].is_string())
        drmCfg.license.unwrapper = STRING::ToLower(jDictLic["unwrapper"].get<std::string>());

      if (jDictLic.contains("unwrapper_params") && jDictLic["unwrapper_params"].is_object())
      {
        for (auto& [paramName, jValue] : jDictLic["unwrapper_params"].items()) // Iterate JSON dict
        {
          if (!(jValue.is_string() || jValue.is_boolean()))
          {
            LOG::LogF(LOGERROR,
                      "The license parameter \"unwrapper_params\" contains invalid values");
            break;
          }

          std::string value;
          if (jValue.is_string())
            value = jValue.get<std::string>();
          else if (jValue.is_boolean())
            value = jValue.get<bool>() ? "true" : "false";

          drmCfg.license.unwrapperParams.emplace(paramName, value);
        }
      }

      if (jDictLic.contains("keyids") && jDictLic["keyids"].is_object())
      {
        for (auto& [kid, jValue] : jDictLic["keyids"].items())
        {
          if (!jValue.is_string())
          {
            LOG::LogF(LOGERROR, "The DRM parameter \"keyids\" contains invalid values");
            break;
          }
          drmCfg.license.keys[kid] = jValue.get<std::string>();
        }
      }
    }
  }

  return true;
}

bool ADP::KODI_PROPS::CCompKodiProps::ParseDrmLegacyConfig(const std::string& data)
{
  // Legacy way to configure a DRM.
  // Designed to have a minimal configuration for the most common use cases using a single DRM.

  /* Expected TEXT structure:
   * [DRM KeySystem] | [License server URL or KeyId's] | [License server headers]
   *
   * From 1 to 3 fields, splitted by pipes
   */

  std::vector<std::string> pipedCfg = STRING::SplitToVec(data, '|');
  if (pipedCfg.size() > 3)
  {
    LOG::LogF(LOGERROR, "Malformed value on the DRM legacy property");
    return false;
  }

  std::string keySystem = STRING::Trim(pipedCfg[0]);

  std::string licenseStr;
  if (pipedCfg.size() > 1)
    licenseStr = STRING::Trim(pipedCfg[1]);

  std::string licenseHeaders;
  if (pipedCfg.size() > 2)
    licenseHeaders = STRING::Trim(pipedCfg[2]);

  if (!DRM::IsValidKeySystem(keySystem))
  {
    LOG::LogF(LOGERROR, "Unknown key system \"%s\" on DRM legacy property", keySystem.data());
    return false;
  }

  DrmCfg drmCfg;
  // As legacy behaviour its expected to force the unique drm configuration available
  drmCfg.priority = 1;

  if (!licenseStr.empty())
  {
    if (URL::IsValidUrl(licenseStr) || URL::IsValidUri(licenseStr)) // License server URI
    {
      drmCfg.license.serverUri = licenseStr;
    }
    else // Assume are keyid's for ClearKey DRM
    {
      // Expected TEXT structure: "kid1:key1,kid2:key2,..."
      std::vector<std::string> keyIdPair = STRING::SplitToVec(licenseStr, ',');

      for (const std::string& keyPairStr : keyIdPair)
      {
        std::vector<std::string> keyPair = STRING::SplitToVec(keyPairStr, ':');
        if (keyPair.size() != 2)
        {
          LOG::LogF(LOGERROR, "Ignored malformed ClearKey kid/key pair");
          continue;
        }
        drmCfg.license.keys[STRING::Trim(keyPair[0])] = STRING::Trim(keyPair[1]);
      }
    }
  }

  ParseHeaderString(drmCfg.license.reqHeaders, licenseHeaders);

  m_drmConfigs[keySystem] = drmCfg;

  return true;
}
