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
constexpr std::string_view PROP_LICENSE_TYPE = "inputstream.adaptive.license_type"; //! @todo: deprecated to be removed on Kodi 23
constexpr std::string_view PROP_LICENSE_KEY = "inputstream.adaptive.license_key"; //! @todo: deprecated to be removed on Kodi 23
// PROP_LICENSE_URL and PROP_LICENSE_URL_APPEND has been added as workaround for Kodi PVR API bug
// where limit property values to max 1024 chars, if exceeds the string is truncated.
// Since some services provide license urls that exceeds 1024 chars,
// PROP_LICENSE_KEY dont have enough space also because include other parameters
// so we provide two properties allow set an url split into two 1024-character parts
// see: https://github.com/xbmc/xbmc/issues/23903#issuecomment-1755264854
// -> this problem has been fixed on Kodi 22
constexpr std::string_view PROP_LICENSE_URL = "inputstream.adaptive.license_url"; //! @todo: deprecated to be removed on Kodi 23
constexpr std::string_view PROP_LICENSE_URL_APPEND = "inputstream.adaptive.license_url_append"; //! @todo: deprecated to be removed on Kodi 23
constexpr std::string_view PROP_LICENSE_DATA = "inputstream.adaptive.license_data"; //! @todo: deprecated to be removed on Kodi 23
constexpr std::string_view PROP_LICENSE_FLAGS = "inputstream.adaptive.license_flags"; //! @todo: deprecated to be removed on Kodi 23
constexpr std::string_view PROP_SERVER_CERT = "inputstream.adaptive.server_certificate"; //! @todo: deprecated to be removed on Kodi 23

constexpr std::string_view PROP_COMMON_HEADERS = "inputstream.adaptive.common_headers";

constexpr std::string_view PROP_MANIFEST_PARAMS = "inputstream.adaptive.manifest_params";
constexpr std::string_view PROP_MANIFEST_HEADERS = "inputstream.adaptive.manifest_headers";
constexpr std::string_view PROP_MANIFEST_UPD_PARAMS = "inputstream.adaptive.manifest_upd_params";
constexpr std::string_view PROP_MANIFEST_CONFIG = "inputstream.adaptive.manifest_config";

constexpr std::string_view PROP_STREAM_PARAMS = "inputstream.adaptive.stream_params";
constexpr std::string_view PROP_STREAM_HEADERS = "inputstream.adaptive.stream_headers";

constexpr std::string_view PROP_AUDIO_LANG_ORIG = "inputstream.adaptive.original_audio_language";
constexpr std::string_view PROP_PLAY_TIMESHIFT_BUFFER = "inputstream.adaptive.play_timeshift_buffer";
constexpr std::string_view PROP_LIVE_DELAY = "inputstream.adaptive.live_delay"; //! @todo: deprecated to be removed on Kodi 23
constexpr std::string_view PROP_PRE_INIT_DATA = "inputstream.adaptive.pre_init_data"; //! @todo: deprecated to be removed on Kodi 23

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

  if (((STRING::KeyExists(props, PROP_LICENSE_TYPE) || STRING::KeyExists(props, PROP_LICENSE_KEY)) &&
       (STRING::KeyExists(props, PROP_DRM_LEGACY) || STRING::KeyExists(props, PROP_DRM))) ||
      (STRING::KeyExists(props, PROP_DRM_LEGACY) && STRING::KeyExists(props, PROP_DRM)))
  {
    LOG::Log(LOGERROR,
             "<<<<<<<<< WRONG DRM CONFIGURATION >>>>>>>>>\n"
             "A mixed use of DRM properties are not supported.\n"
             "Please fix your configuration by using only one of these:\n"
             " - Simple method: \"inputstream.adaptive.drm_legacy\"\n"
             " - Advanced method (deprecated): \"inputstream.adaptive.license_type\" with optional "
             "\"inputstream.adaptive.license_key\"\n"
             " - NEW Advanced method: \"inputstream.adaptive.drm\"\n"
             "FOR MORE INFO, PLEASE READ THE WIKI PAGE: "
             "https://github.com/xbmc/inputstream.adaptive/wiki/Integration-DRM");
    return;
  }

  // If a new DRM property is used, ignore old properties
  if (!STRING::KeyExists(props, PROP_DRM) && !STRING::KeyExists(props, PROP_DRM_LEGACY))
  {
    //! @todo: deprecated DRM properties, all them should be removed
    ParseDrmOldProps(props);
  }

  for (const auto& prop : props)
  {
    const bool isRedacted = !CSrvBroker::GetSettings().IsDebugVerbose();

    if (prop.first == PROP_LICENSE_URL) //! @todo: deprecated to be removed on Kodi 23
    {
      LogProp(prop.first, prop.second, isRedacted);
      LOG::Log(
          LOGWARNING,
          "Warning \"inputstream.adaptive.license_url\" property for PVR API bug is deprecated and "
          "will be removed on next Kodi version. This because the PVR API bug has been fixed on "
          "Kodi v22. Please use the appropriate properties to set the DRM configuration.");
      // If PROP_LICENSE_URL_APPEND is parsed before this one, we need to append it
      licenseUrl = prop.second + licenseUrl;
    }
    else if (prop.first == PROP_LICENSE_URL_APPEND) //! @todo: deprecated to be removed on Kodi 23
    {
      LogProp(prop.first, prop.second, isRedacted);
      LOG::Log(
          LOGWARNING,
          "Warning \"inputstream.adaptive.license_url_append\" property for PVR API bug is deprecated and "
          "will be removed on next Kodi version. This because the PVR API bug has been fixed on "
          "Kodi v22. Please use the appropriate properties to set the DRM configuration.");
      licenseUrl += prop.second;
    }
    else if (prop.first == PROP_COMMON_HEADERS)
    {
      LogProp(prop.first, prop.second);
      ParseHeaderString(m_commonHeaders, prop.second);
    }
    else if (prop.first == PROP_MANIFEST_UPD_PARAMS)
    {
      LogProp(prop.first, prop.second);
      // Should not happen that an add-on try to force the old "full" parameter value
      // of PROP_MANIFEST_UPD_PARAM here but better verify it, in the future this can be removed
      if (prop.second == "full")
        LOG::Log(LOGERROR, "The parameter \"full\" is not supported.");
      else
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
    else if (prop.first == PROP_AUDIO_LANG_ORIG)
    {
      LogProp(prop.first, prop.second);
      m_audioLanguageOrig = prop.second;
    }
    else if (prop.first == PROP_PLAY_TIMESHIFT_BUFFER)
    {
      LogProp(prop.first, prop.second);
      m_playTimeshiftBuffer = STRING::CompareNoCase(prop.second, "true");
    }
    else if (prop.first == PROP_LIVE_DELAY) //! @todo: deprecated to be removed on Kodi 23
    {
      LogProp(prop.first, prop.second);
      LOG::Log(LOGWARNING,
               "Warning \"inputstream.adaptive.live_delay\" property is deprecated and"
               " will be removed next Kodi version, use \"inputstream.adaptive.manifest_config\""
               " instead.\nSee Wiki integration page for more details.");

      m_manifestConfig.liveDelay = STRING::ToUint64(prop.second);
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
      // Ignore legacy DRM props, because has been parsed separately
      if (prop.first == PROP_LICENSE_TYPE || prop.first == PROP_LICENSE_FLAGS ||
          prop.first == PROP_LICENSE_DATA || prop.first == PROP_PRE_INIT_DATA ||
          prop.first == PROP_SERVER_CERT || prop.first == PROP_LICENSE_KEY)
      {
        continue;
      }
      LOG::Log(LOGWARNING, "Property found \"%s\" is not supported", prop.first.c_str());
      continue;
    }
  }

  if (!licenseUrl.empty() && !m_drmConfigs.empty()) //! @todo: deprecated to be removed on Kodi 23
  {
    // PROP_LICENSE_URL replace the license url on the DRM license config
    if (m_drmConfigs.size() > 1)
    {
      LOG::Log(LOGERROR, "The \"inputstream.adaptive.license_url\" and "
                         "\"inputstream.adaptive.license_url_append\" properties\n"
                         "cannot be used with multiple DRM configurations,\n"
                         "Please set a single DRM configuration.");
    }
    else
    {
      auto first = m_drmConfigs.begin();
      first->second.license.serverUri = licenseUrl;
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
    else if (configName == "live_offset" && jValue.is_number_unsigned())
    {
      m_manifestConfig.liveOffset = jValue.get<uint64_t>();
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
    else if (configName == "ignore_fmp4_defaultkid" && jValue.is_boolean())
    {
      m_manifestConfig.ignoreFMP4defaultKid = jValue.get<bool>();
    }
    else
    {
      LOG::LogF(LOGERROR, "Unsupported \"%s\" config or wrong data type on \"%s\" property",
                configName.c_str(), PROP_MANIFEST_CONFIG.data());
    }
  }
}

void ADP::KODI_PROPS::CCompKodiProps::ParseDrmOldProps(
    const std::map<std::string, std::string>& props)
{
  // Translate data from old ISA properties to the new DRM config

  if (!STRING::KeyExists(props, PROP_LICENSE_TYPE))
    return;

  LOG::Log(LOGWARNING, "<<<<<<<<< DEPRECATION NOTICE >>>>>>>>>\n"
                       "DEPRECATED PROPERTIES HAS BEEN USED TO SET THE DRM CONFIGURATION.\n"
                       "THE FOLLOWING PROPERTIES WILL BE REMOVED FROM FUTURE KODI VERSIONS:\n"
                       "- inputstream.adaptive.license_type\n"
                       "- inputstream.adaptive.license_key\n"
                       "- inputstream.adaptive.license_data\n"
                       "- inputstream.adaptive.license_flags\n"
                       "- inputstream.adaptive.server_certificate\n"
                       "- inputstream.adaptive.pre_init_data\n"
                       "YOU SHOULD CONSIDER MIGRATING TO THE NEW PROPERTIES:\n"
                       "- inputstream.adaptive.drm_legacy\n"
                       "- inputstream.adaptive.drm\n"
                       "FOR MORE INFO, PLEASE READ THE WIKI PAGE: "
                       "https://github.com/xbmc/inputstream.adaptive/wiki/Integration-DRM");

  std::string drmKeySystem{DRM::KS_NONE};
  if (STRING::KeyExists(props, PROP_LICENSE_TYPE))
    drmKeySystem = props.at(PROP_LICENSE_TYPE.data());

  LogProp(PROP_LICENSE_TYPE, drmKeySystem);

  if (!DRM::IsValidKeySystem(drmKeySystem))
  {
    LOG::LogF(LOGERROR,
              "Cannot parse DRM configuration, unknown key system \"%s\" on license_type property",
              drmKeySystem.c_str());
    return;
  }

  if (drmKeySystem == DRM::KS_CLEARKEY && STRING::KeyExists(props, PROP_LICENSE_KEY))
  {
    LOG::Log(LOGERROR, "The \"inputstream.adaptive.license_key\" property cannot be used to "
                       "configure ClearKey DRM,\n"
                       "use \"inputstream.adaptive.drm_legacy\" or \"inputstream.adaptive.drm\" "
                       "instead.\nSee Wiki integration page for more details.");
    return;
  }

  // Create a DRM configuration for the specified key system
  DrmCfg& drmCfg = m_drmConfigs[drmKeySystem];
  drmCfg.isNewConfig = false;

  // As legacy behaviour its expected to force the unique drm configuration available
  drmCfg.priority = 1;
  // As legacy behaviour force single session (as in the old ISA versions <= v21)
  drmCfg.isForceSingleSession = true;

  // Parse DRM properties
  const bool isRedacted = !CSrvBroker::GetSettings().IsDebugVerbose();
  std::string propValue;

  if (STRING::GetMapValue(props, PROP_LICENSE_FLAGS, propValue))
  {
    LogProp(PROP_LICENSE_FLAGS, propValue);

    if (propValue.find("persistent_storage") != std::string::npos)
      drmCfg.isPersistentStorage = true;
    if (propValue.find("force_secure_decoder") != std::string::npos)
      drmCfg.isSecureDecoderEnabled = true;
  }

  if (STRING::GetMapValue(props, PROP_LICENSE_DATA, propValue))
  {
    LogProp(PROP_LICENSE_DATA, propValue, isRedacted);
    drmCfg.initData = propValue;
  }

  if (STRING::GetMapValue(props, PROP_PRE_INIT_DATA, propValue))
  {
    LogProp(PROP_PRE_INIT_DATA, propValue, isRedacted);
    drmCfg.preInitData = propValue;
  }

  // Parse DRM license properties

  if (STRING::GetMapValue(props, PROP_SERVER_CERT, propValue))
  {
    LogProp(PROP_SERVER_CERT, propValue, isRedacted);
    drmCfg.license.serverCert = propValue;
  }

  if (STRING::GetMapValue(props, PROP_LICENSE_KEY, propValue))
  {
    LogProp(PROP_LICENSE_KEY, propValue, isRedacted);

    std::vector<std::string> fields = STRING::SplitToVec(propValue, '|');
    size_t fieldCount = fields.size();

    if (drmKeySystem == DRM::KS_NONE)
    {
      // We assume its HLS AES-128 encrypted case
      // where "inputstream.adaptive.license_key" have different fields

      // Field 1: HTTP request params to append to key URL
      if (fieldCount >= 1)
        drmCfg.license.reqParams = fields[0];

      // Field 2: HTTP request headers
      if (fieldCount >= 2)
        ParseHeaderString(drmCfg.license.reqHeaders, fields[1]);
    }
    else
    {
      // Field 1: License server url
      if (fieldCount >= 1)
        drmCfg.license.serverUri = fields[0];

      // Field 2: HTTP request headers
      if (fieldCount >= 2)
        ParseHeaderString(drmCfg.license.reqHeaders, fields[1]);

      // Field 3: HTTP request data (POST request)
      if (fieldCount >= 3)
        drmCfg.license.reqData = fields[2];

      // Field 4: HTTP response data (license wrappers)
      if (fieldCount >= 4)
      {
        bool isJsonWrapper{false};
        std::string jsonWrapperCfg;
        std::string_view wrapperPrefix = fields[3];

        if (wrapperPrefix.empty() || wrapperPrefix == "R")
        {
          // Raw, no wrapper, no-op
        }
        else if (wrapperPrefix == "B")
        {
          drmCfg.license.unwrapper = "base64";
        }
        else if (STRING::StartsWith(wrapperPrefix, "BJ"))
        {
          isJsonWrapper = true;
          drmCfg.license.unwrapper = "base64,json";
          jsonWrapperCfg = wrapperPrefix.substr(2);
        }
        else if (STRING::StartsWith(wrapperPrefix, "JB"))
        {
          isJsonWrapper = true;
          drmCfg.license.unwrapper = "json,base64";
          jsonWrapperCfg = wrapperPrefix.substr(2);
        }
        else if (STRING::StartsWith(wrapperPrefix, "J"))
        {
          isJsonWrapper = true;
          drmCfg.license.unwrapper = "json";
          jsonWrapperCfg = wrapperPrefix.substr(1);
        }
        else if (STRING::StartsWith(wrapperPrefix, "HB"))
        {
          // HB has been removed, we have no info about this use case
          // if someone will open an issue we can try get more info for the reimplementation
          //! @todo: if no feedbacks in future this can be removed, see also todo on decrypters/Helpers.cpp
          LOG::Log(LOGERROR, "The support for \"HB\" parameter in the \"Response data\" field of "
                             "license_key property has been removed. If this is a requirement for "
                             "your video service, let us know by opening an issue on GitHub.");
        }
        else
        {
          LOG::Log(
              LOGERROR,
              "Unknown \"%s\" parameter in the \"response data\" field of license_key property",
              wrapperPrefix.data());
        }

        // Parse JSON configuration
        if (isJsonWrapper)
        {
          if (jsonWrapperCfg.empty())
          {
            LOG::Log(LOGERROR, "Missing JSON dict key names in the \"response data\" field of "
                               "license_key property");
          }
          else
          {
            // Expected format as "KeyNameForData;KeyNameForHDCP" with exact order
            std::vector<std::string> jPaths = STRING::SplitToVec(jsonWrapperCfg, ';');
            // Position 1: The dict Key name to get license data
            if (jPaths.size() >= 1)
            {
              drmCfg.license.unwrapperParams["path_data_traverse"] = "true";
              drmCfg.license.unwrapperParams["path_data"] = jPaths[0];
            }

            // Position 2: The dict Key name to get HDCP value (optional)
            if (jPaths.size() >= 2)
            {
              drmCfg.license.unwrapperParams["path_hdcp_res_traverse"] = "true";
              drmCfg.license.unwrapperParams["path_hdcp_res"] = jPaths[1];
            }
          }
        }
      }
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
