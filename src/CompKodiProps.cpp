/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CompKodiProps.h"

#include "CompSettings.h"
#include "decrypters/Helpers.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <string_view>

#include <rapidjson/document.h>

using namespace UTILS;

namespace
{
// clang-format off
constexpr std::string_view PROP_LICENSE_TYPE = "inputstream.adaptive.license_type"; //! @todo: to be deprecated
constexpr std::string_view PROP_LICENSE_KEY = "inputstream.adaptive.license_key"; //! @todo: to be deprecated
// PROP_LICENSE_URL and PROP_LICENSE_URL_APPEND has been added as workaround for Kodi PVR API bug
// where limit property values to max 1024 chars, if exceeds the string is truncated.
// Since some services provide license urls that exceeds 1024 chars,
// PROP_LICENSE_KEY dont have enough space also because include other parameters
// so we provide two properties allow set an url split into two 1024-character parts
// see: https://github.com/xbmc/xbmc/issues/23903#issuecomment-1755264854
// -> this problem has been fixed on Kodi 22
constexpr std::string_view PROP_LICENSE_URL = "inputstream.adaptive.license_url"; //! @todo: deprecated to be removed on Kodi 23
constexpr std::string_view PROP_LICENSE_URL_APPEND = "inputstream.adaptive.license_url_append"; //! @todo: deprecated to be removed on Kodi 23
constexpr std::string_view PROP_LICENSE_DATA = "inputstream.adaptive.license_data"; //! @todo: to be deprecated
constexpr std::string_view PROP_LICENSE_FLAGS = "inputstream.adaptive.license_flags"; //! @todo: to be deprecated
constexpr std::string_view PROP_SERVER_CERT = "inputstream.adaptive.server_certificate"; //! @todo: to be deprecated

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
constexpr std::string_view PROP_PRE_INIT_DATA = "inputstream.adaptive.pre_init_data"; //! @todo: to be deprecated

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
                        const rapidjson::Value& dictValue,
                        std::string_view keySystem)
{
  if (dictValue.IsObject())
  {
    std::string keys;
    for (auto it = dictValue.MemberBegin(); it != dictValue.MemberEnd(); ++it)
    {
      if (!keys.empty())
        keys += ", ";
      keys += it->name.GetString();
    }
    LOG::Log(LOGDEBUG,
             "Found DRM config for key system: \"%s\" -> Dictionary: \"%s\", Values: \"%s\"",
             keySystem.data(), keyName.data(), keys.c_str());
  }
}
} // unnamed namespace

void ADP::KODI_PROPS::CCompKodiProps::Init(const std::map<std::string, std::string>& props)
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
    bool logPropValRedacted{false};

    if (prop.first == PROP_LICENSE_URL) //! @todo: deprecated to be removed on Kodi 23
    {
      LogProp(prop.first, prop.second, true);
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
      LogProp(prop.first, prop.second, true);
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
      LogProp(prop.first, prop.second, true);
      if (!ParseDrmConfig(prop.second))
        LOG::LogF(LOGERROR, "Cannot parse \"%s\" property, wrong or malformed data.",
          prop.first.c_str());
    }
    else if (prop.first == PROP_DRM_LEGACY && !prop.second.empty())
    {
      LogProp(prop.first, prop.second, true);
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
      first->second.license.serverUrl = licenseUrl;
    }
  }
}

void ADP::KODI_PROPS::CCompKodiProps::ParseConfig(const std::string& data)
{
  /*
   * Expected JSON structure:
   * { "config_name": "value", ... }
   */
  rapidjson::Document jDoc;
  jDoc.Parse(data.c_str(), data.size());

  if (!jDoc.IsObject())
  {
    LOG::LogF(LOGERROR, "Malformed JSON data in to \"%s\" property", PROP_MANIFEST_CONFIG.data());
    return;
  }

  // Iterate dictionary
  for (auto& jChildObj : jDoc.GetObject())
  {
    const std::string configName = jChildObj.name.GetString();
    rapidjson::Value& jDictVal = jChildObj.value;

    if (configName == "ssl_verify_peer" && jDictVal.IsBool())
    {
      m_config.curlSSLVerifyPeer = jDictVal.GetBool();
    }
    else if (configName == "internal_cookies" && jDictVal.IsBool())
    {
      m_config.internalCookies = jDictVal.GetBool();
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
  rapidjson::Document jDoc;
  jDoc.Parse(data.c_str(), data.size());

  if (!jDoc.IsObject())
  {
    LOG::LogF(LOGERROR, "Malformed JSON data in to \"%s\" property", PROP_MANIFEST_CONFIG.data());
    return;
  }

  // Iterate dictionary
  for (auto& jChildObj : jDoc.GetObject())
  {
    const std::string configName = jChildObj.name.GetString();
    rapidjson::Value& jDictVal = jChildObj.value;

    if (configName == "timeshift_bufferlimit" && jDictVal.IsNumber())
    {
      if (jDictVal.GetUint() > 0)
        m_manifestConfig.timeShiftBufferLimit = jDictVal.GetUint();
    }
    else if (configName == "hls_ignore_endlist" && jDictVal.IsBool())
    {
       m_manifestConfig.hlsIgnoreEndList = jDictVal.GetBool();
    }
    else if (configName == "hls_fix_mediasequence" && jDictVal.IsBool())
    {
       m_manifestConfig.hlsFixMediaSequence = jDictVal.GetBool();
    }
    else if (configName == "hls_fix_discsequence" && jDictVal.IsBool())
    {
       m_manifestConfig.hlsFixDiscontSequence = jDictVal.GetBool();
    }
    else if (configName == "live_delay" && jDictVal.IsUint64())
    {
      m_manifestConfig.liveDelay = jDictVal.GetUint64();
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
  /*
   *! @todo: TO UNCOMMENT WHEN DRM AUTO-SELECTION WILL BE FULL IMPLEMENTED
   *
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
                       "- inputstream.adaptive.drm\n"
                       "- inputstream.adaptive.drm_legacy\n"
                       "FOR MORE INFO, PLEASE READ THE WIKI PAGE: "
                       "https://github.com/xbmc/inputstream.adaptive/wiki/Integration-DRM");
  */
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

  // Parse DRM properties
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
    LogProp(PROP_LICENSE_DATA, propValue, true);
    drmCfg.initData = propValue;
  }

  if (STRING::GetMapValue(props, PROP_PRE_INIT_DATA, propValue))
  {
    LogProp(PROP_PRE_INIT_DATA, propValue, true);
    drmCfg.preInitData = propValue;
  }

  // Parse DRM license properties

  if (STRING::GetMapValue(props, PROP_SERVER_CERT, propValue))
  {
    LogProp(PROP_SERVER_CERT, propValue, true);
    drmCfg.license.serverCert = propValue;
  }

  if (STRING::GetMapValue(props, PROP_LICENSE_KEY, propValue))
  {
    LogProp(PROP_LICENSE_KEY, propValue, true);

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
        drmCfg.license.serverUrl = fields[0];

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
              drmCfg.license.unwrapperParams["path_hdcp_traverse"] = "true";
              drmCfg.license.unwrapperParams["path_hdcp"] = jPaths[1];
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
  rapidjson::Document jDoc;
  jDoc.Parse(data.c_str(), data.size());

  if (!jDoc.IsObject())
  {
    LOG::LogF(LOGERROR, "Malformed JSON data in to \"%s\" property", PROP_DRM.data());
    return false;
  }

  // Iterate key systems dict
  for (auto& jChildObj : jDoc.GetObject())
  {
    const char* keySystem = jChildObj.name.GetString();

    if (!DRM::IsValidKeySystem(keySystem))
    {
      LOG::LogF(LOGERROR, "Ignored unknown key system \"%s\" on DRM property", keySystem);
      continue;
    }

    DrmCfg& drmCfg = m_drmConfigs[keySystem];
    auto& jDictVal = jChildObj.value;

    if (!jDictVal.IsObject())
    {
      LOG::LogF(LOGERROR, "Cannot parse key system \"%s\" value on DRM property, wrong data type",
                keySystem);
      continue;
    }

    // Parse main DRM config

    LogDrmJsonDictKeys("main", jDictVal, keySystem);

    if (jDictVal.HasMember("persistent_storage") && jDictVal["persistent_storage"].IsBool())
      drmCfg.isPersistentStorage = jDictVal["persistent_storage"].GetBool();

    if (jDictVal.HasMember("secure_decoder") && jDictVal["secure_decoder"].IsBool())
      drmCfg.isSecureDecoderEnabled = jDictVal["secure_decoder"].GetBool();

    if (jDictVal.HasMember("init_data") && jDictVal["init_data"].IsString())
      drmCfg.initData = jDictVal["init_data"].GetString();

    if (jDictVal.HasMember("pre_init_data") && jDictVal["pre_init_data"].IsString())
      drmCfg.preInitData = jDictVal["pre_init_data"].GetString();

    if (jDictVal.HasMember("optional_key_req_params") &&
        jDictVal["optional_key_req_params"].IsObject())
    {
      for (auto& jPairOptKeyReqParam :
           jDictVal["optional_key_req_params"].GetObject()) // Iterate JSON dict
      {
        if (jPairOptKeyReqParam.name.IsString() && jPairOptKeyReqParam.value.IsString())
        {
          drmCfg.optKeyReqParams.emplace(jPairOptKeyReqParam.name.GetString(),
                                         jPairOptKeyReqParam.value.GetString());
        }
      }
    }

    if (jDictVal.HasMember("priority") && jDictVal["priority"].IsUint())
      drmCfg.priority = jDictVal["priority"].GetUint();

    // Parse license DRM config

    if (jDictVal.HasMember("license") && jDictVal["license"].IsObject())
    {
      auto& jDictLic = jDictVal["license"];

      LogDrmJsonDictKeys("license", jDictLic, keySystem);

      if (jDictLic.HasMember("server_certificate") && jDictLic["server_certificate"].IsString())
        drmCfg.license.serverCert = jDictLic["server_certificate"].GetString();

      if (jDictLic.HasMember("server_url") && jDictLic["server_url"].IsString())
        drmCfg.license.serverUrl = jDictLic["server_url"].GetString();

      if (jDictLic.HasMember("use_http_get_request") && jDictLic["use_http_get_request"].IsBool())
        drmCfg.license.isHttpGetRequest = jDictLic["use_http_get_request"].GetBool();

      if (jDictLic.HasMember("req_headers") && jDictLic["req_headers"].IsString())
        ParseHeaderString(drmCfg.license.reqHeaders, jDictLic["req_headers"].GetString());

      if (jDictLic.HasMember("req_params") && jDictLic["req_params"].IsString())
        drmCfg.license.reqParams = jDictLic["req_params"].GetString();

      if (jDictLic.HasMember("req_data") && jDictLic["req_data"].IsString())
        drmCfg.license.reqData = jDictLic["req_data"].GetString();

      if (jDictLic.HasMember("wrapper") && jDictLic["wrapper"].IsString())
        drmCfg.license.wrapper = STRING::ToLower(jDictLic["wrapper"].GetString());

      if (jDictLic.HasMember("unwrapper") && jDictLic["unwrapper"].IsString())
        drmCfg.license.unwrapper = STRING::ToLower(jDictLic["unwrapper"].GetString());

      if (jDictLic.HasMember("unwrapper_params") && jDictLic["unwrapper_params"].IsObject())
      {
        for (auto& jPairUnwrap : jDictLic["unwrapper_params"].GetObject()) // Iterate JSON dict
        {
          if (jPairUnwrap.name.IsString() && jPairUnwrap.value.IsString())
          {
            drmCfg.license.unwrapperParams.emplace(jPairUnwrap.name.GetString(),
                                                   jPairUnwrap.value.GetString());
          }
        }
      }

      if (jDictLic.HasMember("keyids") && jDictLic["keyids"].IsObject())
      {
        for (auto const& keyid : jDictLic["keyids"].GetObject())
        {
          if (keyid.name.IsString() && keyid.value.IsString())
            drmCfg.license.keys[keyid.name.GetString()] = (keyid.value.GetString());
        }
      }
    }

    //! @todo: temporary support only one DRM config, must be reworked the CSession for DRM auto-selection
    break;
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
    if (URL::IsValidUrl(licenseStr)) // License server URL
    {
      drmCfg.license.serverUrl = licenseStr;
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
