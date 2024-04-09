/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CompKodiProps.h"
#include "CompSettings.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <rapidjson/document.h>

#include <string_view>

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

constexpr std::string_view PROP_INTERNAL_COOKIES = "inputstream.adaptive.internal_cookies";

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

  for (const auto& prop : props)
  {
    bool logPropValRedacted{false};

    if (prop.first == PROP_LICENSE_TYPE)
    {
      m_licenseType = prop.second;
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
    else if (prop.first == PROP_INTERNAL_COOKIES)
    {
      m_isInternalCookies = STRING::CompareNoCase(prop.second, "true");
    }
    else if (prop.first == PROP_MANIFEST_CONFIG)
    {
      ParseManifestConfig(prop.second);
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
    else
    {
      LOG::LogF(LOGERROR, "Unsupported \"%s\" config or wrong data type on \"%s\" property",
                configName.c_str(), PROP_MANIFEST_CONFIG.data());
    }
  }
}
