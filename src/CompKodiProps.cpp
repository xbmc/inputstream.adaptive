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
constexpr std::string_view PROP_LICENSE_TYPE = "inputstream.adaptive.license_type";
constexpr std::string_view PROP_LICENSE_KEY = "inputstream.adaptive.license_key";
// PROP_LICENSE_URL and PROP_LICENSE_URL_APPEND has been added as workaround for Kodi PVR API bug
// where limit property values to max 1024 chars, if exceeds the string is truncated.
// Since some services provide license urls that exceeds 1024 chars,
// PROP_LICENSE_KEY dont have enough space also because include other parameters
// so we provide two properties allow set an url split into two 1024-character parts
// see: https://github.com/xbmc/xbmc/issues/23903#issuecomment-1755264854
// this problem should be fixed on Kodi 22
constexpr std::string_view PROP_LICENSE_URL = "inputstream.adaptive.license_url";
constexpr std::string_view PROP_LICENSE_URL_APPEND = "inputstream.adaptive.license_url_append";
constexpr std::string_view PROP_LICENSE_DATA = "inputstream.adaptive.license_data";
constexpr std::string_view PROP_LICENSE_FLAGS = "inputstream.adaptive.license_flags";
constexpr std::string_view PROP_SERVER_CERT = "inputstream.adaptive.server_certificate";

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
constexpr std::string_view PROP_PRE_INIT_DATA = "inputstream.adaptive.pre_init_data";

constexpr std::string_view PROP_CONFIG = "inputstream.adaptive.config";
constexpr std::string_view PROP_DRM = "inputstream.adaptive.drm";
constexpr std::string_view PROP_DRM_LEGACY = "inputstream.adaptive.drm_legacy";

constexpr std::string_view PROP_INTERNAL_COOKIES = "inputstream.adaptive.internal_cookies"; //! @todo: to remove on Kodi 22

// Chooser's properties
constexpr std::string_view PROP_STREAM_SELECTION_TYPE = "inputstream.adaptive.stream_selection_type";
constexpr std::string_view PROP_CHOOSER_BANDWIDTH_MAX = "inputstream.adaptive.chooser_bandwidth_max";
constexpr std::string_view PROP_CHOOSER_RES_MAX = "inputstream.adaptive.chooser_resolution_max";
constexpr std::string_view PROP_CHOOSER_RES_SECURE_MAX = "inputstream.adaptive.chooser_resolution_secure_max";
// clang-format on
} // unnamed namespace

ADP::KODI_PROPS::CCompKodiProps::CCompKodiProps(const std::map<std::string, std::string>& props)
{
  std::string licenseUrl;

  if ((STRING::KeyExists(props, PROP_LICENSE_TYPE) || STRING::KeyExists(props, PROP_LICENSE_KEY)) &&
      STRING::KeyExists(props, PROP_DRM_LEGACY))
  {
    LOG::Log(LOGERROR, "WRONG DRM CONFIGURATION. A mixed use of DRM properties are not supported.\n"
                       "Please fix your configuration by setting only one of these:\n"
                       " - Simple method: \"inputstream.adaptive.drm_legacy\"\n"
                       " - Advanced method: \"inputstream.adaptive.license_type\" with optional "
                       "\"inputstream.adaptive.license_key\"\n"
                       "For more details, see the Github Wiki Integration page.");
    return;
  }

  for (const auto& prop : props)
  {
    bool logPropValRedacted{false};

    if (prop.first == PROP_LICENSE_TYPE)
    {
      if (DRM::IsKeySystemSupported(prop.second))
        m_licenseType = prop.second;
      else
        LOG::LogF(LOGERROR, "License type \"%s\" is not supported", prop.second.c_str());
    }
    else if (prop.first == PROP_LICENSE_KEY)
    {
      m_licenseKey = prop.second;
      logPropValRedacted = true;
    }
    else if (prop.first == PROP_LICENSE_URL)
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
    else if (prop.first == PROP_LICENSE_DATA)
    {
      m_licenseData = prop.second;
      logPropValRedacted = true;
    }
    else if (prop.first == PROP_LICENSE_FLAGS)
    {
      if (prop.second.find("persistent_storage") != std::string::npos)
        m_isLicensePersistentStorage = true;
      if (prop.second.find("force_secure_decoder") != std::string::npos)
        m_isLicenseForceSecureDecoder = true;
    }
    else if (prop.first == PROP_SERVER_CERT)
    {
      m_serverCertificate = prop.second;
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
    else if (prop.first == PROP_PRE_INIT_DATA)
    {
      m_drmPreInitData = prop.second;
      logPropValRedacted = true;
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
    else if (prop.first == PROP_CONFIG)
    {
      ParseConfig(prop.second);
    }
    else if (prop.first == PROP_INTERNAL_COOKIES)
    {
      LOG::Log(LOGERROR,
               "Warning \"inputstream.adaptive.internal_cookies\" property has been moved to the new "
               "\"inputstream.adaptive.config\". The old property will be removed from next Kodi 22.\n"
               "See Wiki integration page for more details.");
      m_config.internalCookies = STRING::CompareNoCase(prop.second, "true");
    }
    else if (prop.first == PROP_MANIFEST_CONFIG)
    {
      ParseManifestConfig(prop.second);
    }
    /* for future DRM properties rework
    else if (prop.first == PROP_DRM && !prop.second.empty())
    {
      if (!ParseDrmConfig(prop.second))
        LOG::LogF(LOGERROR, "Cannot parse \"%s\" property, wrong or malformed data.",
          prop.first.c_str());

      logPropValRedacted = true;
    }
    */
    else if (prop.first == PROP_DRM_LEGACY && !prop.second.empty())
    {
      if (!ParseDrmLegacyConfig(prop.second))
        LOG::LogF(LOGERROR, "Cannot parse \"%s\" property, wrong or malformed data.",
                  prop.first.c_str());

      logPropValRedacted = true;
    }
    else
    {
      LOG::Log(LOGWARNING, "Property found \"%s\" is not supported", prop.first.c_str());
      continue;
    }

    LOG::Log(LOGDEBUG, "Property found \"%s\" value: %s", prop.first.c_str(),
             logPropValRedacted ? "[redacted]" : prop.second.c_str());
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

  if (m_licenseType == DRM::KS_CLEARKEY && !m_licenseKey.empty())
  {
    LOG::Log(
      LOGERROR,
      "The \"inputstream.adaptive.license_key\" property cannot be used to configure ClearKey DRM,\n"
      "use \"inputstream.adaptive.drm_legacy\" instead.\nSee Wiki integration page for more details.");
    m_licenseKey.clear();
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
    else
    {
      LOG::LogF(LOGERROR, "Unsupported \"%s\" config or wrong data type on \"%s\" property",
                configName.c_str(), PROP_MANIFEST_CONFIG.data());
    }
  }
}

//! @todo: inputstream.adaptive.drm in future will be used to set configs of all DRM's
//!        and so will replace old props such as: license_type, license_key, license_data, etc...
bool ADP::KODI_PROPS::CCompKodiProps::ParseDrmConfig(const std::string& data)
{
  /* Expected JSON structure:
   * { "keysystem_name" : { "persistent_storage" : bool,
   *                        "force_secure_decoder" : bool,
   *                        "streams_pssh_data" : str,
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

    if (!DRM::IsKeySystemSupported(keySystem))
    {
      LOG::LogF(LOGERROR, "Ignored unknown key system \"%s\" on DRM property", keySystem);
      continue;
    }

    //! @todo: m_licenseType temporarily assigned, to remove with the DRM config rework
    m_licenseType = keySystem;

    DrmCfg& drmCfg = m_drmConfigs[keySystem];
    auto& jDictVal = jChildObj.value;

    if (!jDictVal.IsObject())
    {
      LOG::LogF(LOGERROR, "Cannot parse key system \"%s\" value on DRM property, wrong data type",
                keySystem);
      continue;
    }

    if (jDictVal.HasMember("license") && jDictVal["license"].IsObject())
    {
      auto& jDictLic = jDictVal["license"];

      if (jDictLic.HasMember("keyids") && jDictLic["keyids"].IsArray())
      {
        for (auto const& keyid : jDictLic["keyids"].GetObject())
        {
          if (keyid.name.IsString() && keyid.value.IsString())
            drmCfg.license.keys[keyid.name.GetString()] = (keyid.value.GetString());
        }
      }
    }

    //! @todo: temporary support only one DRM config
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

  if (!DRM::IsKeySystemSupported(keySystem))
  {
    LOG::LogF(LOGERROR, "Unknown key system \"%s\" on DRM legacy property", keySystem.data());
    return false;
  }

  m_licenseType = keySystem;
  std::string licenseUrl;

  if (!licenseStr.empty())
  {
    if (URL::IsValidUrl(licenseStr)) // License server URL
    {
      licenseUrl = licenseStr;
    }
    else // Assume are keyid's for ClearKey DRM
    {
      // Expected TEXT structure: "kid1:key1,kid2:key2,..."
      DrmCfg& drmCfg = m_drmConfigs[keySystem];
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

  if (keySystem == DRM::KS_CLEARKEY)
  {
    DrmCfg& drmCfg = m_drmConfigs[keySystem];

    drmCfg.license.serverUrl = licenseUrl;
    ParseHeaderString(drmCfg.license.reqHeaders, licenseHeaders);
    // Until the future DRM config rework only the ClearKey DRM use the new properties
    // so return now to keep m_licenseKey empty
    return true;
  }
  else if (licenseHeaders.empty())
  {
    //! @todo: temporary stored default DRM values here just for convenience
    //! since we need to construct the "license key" string
    //! these values are stored also on DRM's implementation,
    //! they must be placed in an appropriate place with the future DRM config rework
    if (keySystem == DRM::KS_WIDEVINE)
      licenseHeaders = "Content-Type=application%2Foctet-stream";
    else if (keySystem == DRM::KS_PLAYREADY)
      licenseHeaders = "Content-Type=text%2Fxml&SOAPAction=http%3A%2F%2Fschemas.microsoft.com%"
                       "2FDRM%2F2007%2F03%2Fprotocols%2FAcquireLicense";
    else if (keySystem == DRM::KS_WISEPLAY)
      licenseHeaders = "Content-Type=application/json";
  }

  m_licenseKey = licenseUrl + "|" + licenseHeaders + "|R{SSM}|R";
  return true;
}
