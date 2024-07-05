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
#include "utils/Utils.h"
#include "utils/log.h"

#include <rapidjson/document.h>

#include <string_view>

using namespace UTILS;

namespace
{
// clang-format off
constexpr std::string_view PROP_LICENSE_TYPE = "inputstream.adaptive.license_type"; //! @todo: deprecated, to be removed on next Kodi release
constexpr std::string_view PROP_LICENSE_KEY = "inputstream.adaptive.license_key"; //! @todo: deprecated, to be removed on next Kodi release
// PROP_LICENSE_URL and PROP_LICENSE_URL_APPEND has been added as workaround for Kodi PVR API bug
// where limit property values to max 1024 chars, if exceeds the string is truncated.
// Since some services provide license urls that exceeds 1024 chars,
// PROP_LICENSE_KEY dont have enough space also because include other parameters
// so we provide two properties allow set an url split into two 1024-character parts
// see: https://github.com/xbmc/xbmc/issues/23903#issuecomment-1755264854
// this problem should be fixed on Kodi 22
constexpr std::string_view PROP_LICENSE_URL = "inputstream.adaptive.license_url";
constexpr std::string_view PROP_LICENSE_URL_APPEND = "inputstream.adaptive.license_url_append";
constexpr std::string_view PROP_LICENSE_DATA = "inputstream.adaptive.license_data"; //! @todo: deprecated, to be removed on next Kodi release
constexpr std::string_view PROP_LICENSE_FLAGS = "inputstream.adaptive.license_flags"; //! @todo: deprecated, to be removed on next Kodi release
constexpr std::string_view PROP_SERVER_CERT = "inputstream.adaptive.server_certificate"; //! @todo: deprecated, to be removed on next Kodi release

constexpr std::string_view PROP_MANIFEST_TYPE = "inputstream.adaptive.manifest_type"; //! @todo: deprecated, to be removed on next Kodi release
constexpr std::string_view PROP_MANIFEST_UPD_PARAM = "inputstream.adaptive.manifest_update_parameter"; //! @todo: deprecated, to be removed on next Kodi release
constexpr std::string_view PROP_MANIFEST_PARAMS = "inputstream.adaptive.manifest_params";
constexpr std::string_view PROP_MANIFEST_HEADERS = "inputstream.adaptive.manifest_headers";
constexpr std::string_view PROP_MANIFEST_UPD_PARAMS = "inputstream.adaptive.manifest_upd_params";
constexpr std::string_view PROP_MANIFEST_CONFIG = "inputstream.adaptive.manifest_config";

constexpr std::string_view PROP_STREAM_PARAMS = "inputstream.adaptive.stream_params";
constexpr std::string_view PROP_STREAM_HEADERS = "inputstream.adaptive.stream_headers";

constexpr std::string_view PROP_AUDIO_LANG_ORIG = "inputstream.adaptive.original_audio_language";
constexpr std::string_view PROP_PLAY_TIMESHIFT_BUFFER = "inputstream.adaptive.play_timeshift_buffer";
constexpr std::string_view PROP_LIVE_DELAY = "inputstream.adaptive.live_delay";
constexpr std::string_view PROP_PRE_INIT_DATA = "inputstream.adaptive.pre_init_data"; //! @todo: deprecated, to be removed on next Kodi release

constexpr std::string_view PROP_DRM = "inputstream.adaptive.drm";
constexpr std::string_view PROP_DRM_LICENSE = "inputstream.adaptive.drm_license";

constexpr std::string_view PROP_INTERNAL_COOKIES = "inputstream.adaptive.internal_cookies";

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

void LogDrmJsonDictKeys(const rapidjson::Value& dict, std::string_view keySystem)
{
  if (dict.IsObject())
  {
    std::string keys;
    for (auto it = dict.MemberBegin(); it != dict.MemberEnd(); ++it)
    {
      keys += it->name.GetString();
      keys += "; ";
    }
    LOG::Log(LOGDEBUG, "DRM config for key system: \"%s\", parameters: %s", keySystem.data(),
             keys.c_str());
  }
}
} // unnamed namespace

ADP::KODI_PROPS::CCompKodiProps::CCompKodiProps(const std::map<std::string, std::string>& props)
{
  std::string licenseUrl;

  bool isNewDrmPropsSet =
      STRING::KeyExists(props, PROP_DRM) || STRING::KeyExists(props, PROP_DRM_LICENSE);
  if (!isNewDrmPropsSet)
  {
    //! @todo: deprecated DRM properties, all them should be removed on next Kodi version.
    ParseLegacyDrm(props);
  }

  for (const auto& prop : props)
  {
    bool logPropValRedacted{false};

    if (prop.first == PROP_LICENSE_URL)
    {
      // If PROP_LICENSE_URL_APPEND is parsed before this one, we need to append it
      licenseUrl = prop.second + licenseUrl;
      logPropValRedacted = true;
    }
    else if (prop.first == PROP_LICENSE_URL_APPEND)
    {
      licenseUrl += prop.second;
      logPropValRedacted = true;
    }
    else if (prop.first == PROP_MANIFEST_TYPE) //! @todo: deprecated, to be removed on next Kodi release
    {
      LOG::Log(
          LOGWARNING,
          "Warning \"inputstream.adaptive.manifest_type\" property is deprecated and "
          "will be removed next Kodi version, the manifest type is now automatically detected.\n"
          "If you are using a proxy remember to add the appropriate \"content-type\" header "
          "to the HTTP manifest response\nSee Wiki page \"How to provide custom manifest/license\" "
          "to learn more about it.");

      if (STRING::CompareNoCase(prop.second, "MPD"))
        m_manifestType = ManifestType::MPD;
      else if (STRING::CompareNoCase(prop.second, "ISM"))
        m_manifestType = ManifestType::ISM;
      else if (STRING::CompareNoCase(prop.second, "HLS"))
        m_manifestType = ManifestType::HLS;
      else
        LOG::LogF(LOGERROR, "Manifest type \"%s\" is not supported", prop.second.c_str());
    }
    else if (prop.first ==
             PROP_MANIFEST_UPD_PARAM) //! @todo: deprecated, to be removed on next Kodi release
    {
      LOG::Log(
          LOGWARNING,
          "Warning \"inputstream.adaptive.manifest_update_parameter\" property is deprecated and"
          " will be removed next Kodi version, use \"inputstream.adaptive.manifest_upd_params\""
          " instead.\nSee Wiki integration page for more details.");
      if (prop.second == "full")
      {
        LOG::Log(LOGERROR, "The parameter \"full\" is no longer supported. For problems with live "
                           "streaming contents please open an Issue to the GitHub repository.");
      }
      else
        m_manifestUpdateParam = prop.second;
    }
    else if (prop.first == PROP_MANIFEST_UPD_PARAMS)
    {
      // Should not happen that an add-on try to force the old "full" parameter value
      // of PROP_MANIFEST_UPD_PARAM here but better verify it, in the future this can be removed
      if (prop.second == "full")
        LOG::Log(LOGERROR, "The parameter \"full\" is not supported.");
      else
        m_manifestUpdParams = prop.second;
    }
    else if (prop.first == PROP_MANIFEST_PARAMS)
    {
      m_manifestParams = prop.second;
    }
    else if (prop.first == PROP_MANIFEST_HEADERS)
    {
      ParseHeaderString(m_manifestHeaders, prop.second);
    }
    else if (prop.first == PROP_STREAM_PARAMS)
    {
      m_streamParams = prop.second;
    }
    else if (prop.first == PROP_STREAM_HEADERS)
    {
      ParseHeaderString(m_streamHeaders, prop.second);
    }
    else if (prop.first == PROP_AUDIO_LANG_ORIG)
    {
      m_audioLanguageOrig = prop.second;
    }
    else if (prop.first == PROP_PLAY_TIMESHIFT_BUFFER)
    {
      m_playTimeshiftBuffer = STRING::CompareNoCase(prop.second, "true");
    }
    else if (prop.first == PROP_LIVE_DELAY)
    {
      m_liveDelay = STRING::ToUint64(prop.second); //! @todo: move to PROP_MANIFEST_CONFIG
    }
    else if (prop.first == PROP_STREAM_SELECTION_TYPE)
    {
      m_chooserProps.m_chooserType = prop.second;
    }
    else if (prop.first == PROP_CHOOSER_BANDWIDTH_MAX)
    {
      m_chooserProps.m_bandwidthMax = static_cast<uint32_t>(std::stoi(prop.second));
    }
    else if (prop.first == PROP_CHOOSER_RES_MAX)
    {
      std::pair<int, int> res;
      if (STRING::GetMapValue(ADP::SETTINGS::RES_CONV_LIST, prop.second, res))
        m_chooserProps.m_resolutionMax = res;
      else
        LOG::Log(LOGERROR, "Resolution not valid on \"%s\" property.", prop.first.c_str());
    }
    else if (prop.first == PROP_CHOOSER_RES_SECURE_MAX)
    {
      std::pair<int, int> res;
      if (STRING::GetMapValue(ADP::SETTINGS::RES_CONV_LIST, prop.second, res))
        m_chooserProps.m_resolutionSecureMax = res;
      else
        LOG::Log(LOGERROR, "Resolution not valid on \"%s\" property.", prop.first.c_str());
    }
    else if (prop.first == PROP_INTERNAL_COOKIES)
    {
      m_isInternalCookies = STRING::CompareNoCase(prop.second, "true");
    }
    else if (prop.first == PROP_MANIFEST_CONFIG)
    {
      ParseManifestConfig(prop.second);
    }
    else if (prop.first == PROP_DRM && !prop.second.empty())
    {
      if (!ParseDrm(prop.second))
        LOG::LogF(LOGERROR, "Cannot parse \"%s\" property, wrong or malformed data.",
                  prop.first.c_str());

      logPropValRedacted = true;
    }
    else if (prop.first == PROP_DRM_LICENSE && !prop.second.empty())
    {
      if (!ParseDrmLicense(prop.second))
        LOG::LogF(LOGERROR, "Cannot parse \"%s\" property, wrong or malformed data.",
                  prop.first.c_str());

      logPropValRedacted = true;
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

    LogProp(prop.first, prop.second, logPropValRedacted);
  }

  if (!licenseUrl.empty())
  {
    // PROP_LICENSE_URL replace the license url contained into PROP_LICENSE_KEY
    const size_t pipePos = m_licenseKey.find('|');
    if (pipePos == std::string::npos)
      m_licenseKey = licenseUrl;
    else
      m_licenseKey.replace(0, pipePos, licenseUrl);
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
    else
    {
      LOG::LogF(LOGERROR, "Unsupported \"%s\" config or wrong data type on \"%s\" property",
                configName.c_str(), PROP_MANIFEST_CONFIG.data());
    }
  }
}

void ADP::KODI_PROPS::CCompKodiProps::ParseLegacyDrm(
    const std::map<std::string, std::string>& props)
{
  // Translate data from old ISA properties to the new DRM configuration,
  // Proceed only if "inputstream.adaptive.license_key" is set
  if (!STRING::KeyExists(props, PROP_LICENSE_KEY))
    return;

  LOG::Log(
      LOGWARNING,
      "<<< PROPERTIES DEPRECATION NOTICE >>>\n"
      "DEPRECATED PROPERTIES HAS BEEN USED TO SET THE DRM CONFIGURATION.\n"
      "THE FOLLOWING PROPERTIES WILL BE REMOVED STARTING FROM KODI 22:\n"
      "- inputstream.adaptive.license_type\n"
      "- inputstream.adaptive.license_key\n"
      "- inputstream.adaptive.license_data\n"
      "- inputstream.adaptive.license_flags\n"
      "- inputstream.adaptive.server_certificate\n"
      "- inputstream.adaptive.pre_init_data\n"
      "TO AVOID PLAYBACK FAILURES, YOU SHOULD INCLUDE THE NEW PROPERTIES AS SOON AS POSSIBLE:\n"
      "- inputstream.adaptive.drm\n"
      "- inputstream.adaptive.drm_license\n"
      "FOR MORE INFO, PLEASE READ INPUTSTREAM ADAPTIVE WIKI ON GITHUB.");

  std::string drmKeySystem = props.at(PROP_LICENSE_TYPE.data());
  LogProp(PROP_LICENSE_TYPE, drmKeySystem);

  if (drmKeySystem.empty())
    drmKeySystem = DRM::KS_NONE;

  if (!DRM::IsKeySystemSupported(drmKeySystem))
  {
    LOG::LogF(LOGERROR,
              "Cannot parse DRM configuration, unknown key system \"%s\" on license_type property",
              drmKeySystem.c_str());
    return;
  }

  // Create a DRM configuration for the specified key system
  DrmCfg& drmCfg = m_drmConfigs[drmKeySystem];

  drmCfg.m_priority = 1;
  std::string propValue;

  // Parse DRM properties

  if (STRING::GetMapValue(props, std::string(PROP_LICENSE_FLAGS), propValue))
  {
    if (propValue.find("persistent_storage") != std::string::npos)
      drmCfg.m_isPersistentStorage = true;
    if (propValue.find("force_secure_decoder") != std::string::npos)
      drmCfg.m_isSecureDecoderForced = true;

    LogProp(PROP_LICENSE_FLAGS, propValue);
  }

  if (STRING::GetMapValue(props, std::string(PROP_LICENSE_DATA), propValue))
  {
    drmCfg.m_streamsPsshData = propValue;
    LogProp(PROP_LICENSE_DATA, propValue, true);
  }

  if (STRING::GetMapValue(props, std::string(PROP_PRE_INIT_DATA), propValue))
  {
    drmCfg.m_preInitData = propValue;
    LogProp(PROP_PRE_INIT_DATA, propValue, true);
  }

  // Parse DRM license properties

  if (STRING::GetMapValue(props, std::string(PROP_SERVER_CERT), propValue))
  {
    drmCfg.m_license.m_serverCertificate = propValue;
    LogProp(PROP_SERVER_CERT, propValue, true);
  }

  if (STRING::GetMapValue(props, std::string(PROP_LICENSE_KEY), propValue))
  {
    std::vector<std::string> fields = STRING::SplitToVec(propValue, '|');
    size_t fieldCount = fields.size();

    if (drmKeySystem == DRM::KS_NONE)
    {
      // We assume its HLS AES-128 encrypted case
      // where "inputstream.adaptive.license_key" have different fields

      // Field 1: HTTP request params to append to key URL
      if (fieldCount >= 1)
        drmCfg.m_license.m_reqParams = fields[0];

      // Field 2: HTTP request headers
      if (fieldCount >= 2)
        ParseHeaderString(drmCfg.m_license.m_reqHeaders, fields[1]);
    }
    else
    {
      // Field 1: License server url
      if (fieldCount >= 1)
        drmCfg.m_license.m_serverUrl = fields[0];

      // Field 2: HTTP request headers
      if (fieldCount >= 2)
        ParseHeaderString(drmCfg.m_license.m_reqHeaders, fields[1]);

      // Field 3: HTTP request data (POST request)
      if (fieldCount >= 3)
        drmCfg.m_license.m_reqData = fields[2];

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
          drmCfg.m_license.m_wrapper = "base64";
        }
        else if (STRING::StartsWith(wrapperPrefix, "BJ"))
        {
          isJsonWrapper = true;
          drmCfg.m_license.m_wrapper = "base64+json";
          jsonWrapperCfg = wrapperPrefix.substr(2);
        }
        else if (STRING::StartsWith(wrapperPrefix, "JB"))
        {
          isJsonWrapper = true;
          drmCfg.m_license.m_wrapper = "json+base64";
          jsonWrapperCfg = wrapperPrefix.substr(2);
        }
        else if (STRING::StartsWith(wrapperPrefix, "J"))
        {
          isJsonWrapper = true;
          drmCfg.m_license.m_wrapper = "json";
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
            drmCfg.m_license.m_isJsonPathTraverse = true;
            // Expected format as "KeyNameForData;KeyNameForHDCP" with exact order
            std::vector<std::string> jPaths = STRING::SplitToVec(jsonWrapperCfg, ';');
            // Position 1: The dict Key name to get license data
            if (jPaths.size() >= 1)
              drmCfg.m_license.m_wrapperParams.emplace("path_data", jPaths[0]);

            // Position 2: The dict Key name to get HDCP value (optional)
            if (jPaths.size() >= 2)
              drmCfg.m_license.m_wrapperParams.emplace("path_hdcp", jPaths[1]);
          }
        }
      }
    }
    LogProp(PROP_LICENSE_KEY, propValue, true);
  }
}

bool ADP::KODI_PROPS::CCompKodiProps::ParseDrm(std::string data)
{
  /* Expected JSON structure:
   * { "keysystem_name" : { "persistent_storage" : bool,
   *                        "force_secure_decoder" : bool,
   *                        "streams_pssh_data" : str,
   *                        "pre_init_data" : str,
   *                        "priority": int },
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

    if (!DRM::IsKeySystemSupported(keySystem))
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

    if (jDictVal.HasMember("persistent_storage") && jDictVal["persistent_storage"].IsBool())
      drmCfg.m_isPersistentStorage = jDictVal["persistent_storage"].GetBool();

    if (jDictVal.HasMember("force_secure_decoder") && jDictVal["force_secure_decoder"].IsBool())
      drmCfg.m_isPersistentStorage = jDictVal["force_secure_decoder"].GetBool();

    if (jDictVal.HasMember("streams_pssh_data") && jDictVal["streams_pssh_data"].IsString())
      drmCfg.m_streamsPsshData = jDictVal["streams_pssh_data"].GetString();

    if (jDictVal.HasMember("pre_init_data") && jDictVal["pre_init_data"].IsString())
      drmCfg.m_preInitData = jDictVal["pre_init_data"].GetString();

    if (jDictVal.HasMember("priority") && jDictVal["priority"].IsInt())
      drmCfg.m_priority = jDictVal["priority"].GetInt();

    LogDrmJsonDictKeys(
        jDictVal,
        keySystem); // todo: <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< comment this, maybe an appropriate config log elsewere
  }

  return true;
}

bool ADP::KODI_PROPS::CCompKodiProps::ParseDrmLicense(std::string data)
{
  /* Expected JSON structure:
   * { "keysystem_name" : { "wrapper" : str,
   *                        "wrapper_params" : dict,
   *                        "server_certificate" : str,
   *                        "server_url" : str,
   *                        "req_headers" : str,
   *                        "req_params" : str,
   *                        "req_data" : str },
   *   "keysystem_name_2" : { ... }}
   */
  rapidjson::Document jDoc;
  jDoc.Parse(data.c_str(), data.size());

  if (!jDoc.IsObject())
  {
    LOG::LogF(LOGERROR, "Malformed JSON data in to \"%s\" property", PROP_DRM_LICENSE.data());
    return false;
  }

  // Iterate key system dict
  for (auto& jChildObj : jDoc.GetObject())
  {
    const char* keySystem = jChildObj.name.GetString();

    if (!DRM::IsKeySystemSupported(keySystem))
    {
      LOG::LogF(LOGERROR, "Ignored unknown key system \"%s\" on DRM license property", keySystem);
      continue;
    }

    DrmCfg& drmCfg = m_drmConfigs[keySystem];
    auto& jDictVal = jChildObj.value;

    if (!jDictVal.IsObject())
    {
      LOG::LogF(LOGERROR,
                "Cannot parse key system \"%s\" value on DRM license property, wrong data type",
                keySystem);
      continue;
    }

    if (jDictVal.HasMember("wrapper") && jDictVal["wrapper"].IsString())
    {
      drmCfg.m_license.m_wrapper = STRING::ToLower(jDictVal["wrapper"].GetString());
    }

    if (jDictVal.HasMember("wrapper_params") && jDictVal["wrapper_params"].IsObject())
    {
      // Iterate wrapper_params dict
      for (auto& cWrapParam : jDictVal["wrapper_params"].GetObject())
      {
        if (cWrapParam.name.IsString() && cWrapParam.value.IsString())
        {
          drmCfg.m_license.m_wrapperParams.emplace(cWrapParam.name.GetString(),
                                                   cWrapParam.value.GetString());
        }
      }
    }

    if (jDictVal.HasMember("server_certificate") && jDictVal["server_certificate"].IsString())
      drmCfg.m_license.m_serverCertificate = jDictVal["server_certificate"].GetString();

    if (jDictVal.HasMember("server_url") && jDictVal["server_url"].IsString())
      drmCfg.m_license.m_serverUrl = jDictVal["server_url"].GetString();

    if (jDictVal.HasMember("req_headers") && jDictVal["req_headers"].IsString())
      ParseHeaderString(drmCfg.m_license.m_reqHeaders, jDictVal["req_headers"].GetString());

    if (jDictVal.HasMember("req_params") && jDictVal["req_params"].IsString())
      drmCfg.m_license.m_reqParams = jDictVal["req_params"].GetString();

    if (jDictVal.HasMember("req_data") && jDictVal["req_data"].IsString())
      drmCfg.m_license.m_reqData = jDictVal["req_data"].GetString();

    LogDrmJsonDictKeys(
        jDictVal,
        keySystem); // todo: <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< comment this, maybe an appropriate config log elsewere
  }

  return true;
}
