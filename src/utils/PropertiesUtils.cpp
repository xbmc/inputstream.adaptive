/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "PropertiesUtils.h"

#include "SettingsUtils.h"
#include "Utils.h"
#include "kodi/tools/StringUtils.h"
#include "log.h"

#include <string_view>

using namespace UTILS::PROPERTIES;
using namespace kodi::tools;

namespace
{
// clang-format off
constexpr std::string_view PROP_LICENSE_TYPE = "inputstream.adaptive.license_type";
constexpr std::string_view PROP_LICENSE_KEY = "inputstream.adaptive.license_key";
constexpr std::string_view PROP_LICENSE_DATA = "inputstream.adaptive.license_data";
constexpr std::string_view PROP_LICENSE_FLAGS = "inputstream.adaptive.license_flags";
constexpr std::string_view PROP_SERVER_CERT = "inputstream.adaptive.server_certificate";
constexpr std::string_view PROP_MANIFEST_TYPE = "inputstream.adaptive.manifest_type";
constexpr std::string_view PROP_MANIFEST_UPD_PARAM = "inputstream.adaptive.manifest_update_parameter";
constexpr std::string_view PROP_STREAM_HEADERS = "inputstream.adaptive.stream_headers";
constexpr std::string_view PROP_AUDIO_LANG_ORIG = "inputstream.adaptive.original_audio_language";
constexpr std::string_view PROP_BANDWIDTH_MAX = "inputstream.adaptive.max_bandwidth"; //! @todo: deprecated, to be removed on next Kodi release
constexpr std::string_view PROP_PLAY_TIMESHIFT_BUFFER = "inputstream.adaptive.play_timeshift_buffer";
constexpr std::string_view PROP_PRE_INIT_DATA = "inputstream.adaptive.pre_init_data";

// Chooser's properties
constexpr std::string_view PROP_STREAM_SELECTION_TYPE = "inputstream.adaptive.stream_selection_type";
constexpr std::string_view PROP_CHOOSER_BANDWIDTH_MAX = "inputstream.adaptive.chooser_bandwidth_max";
constexpr std::string_view PROP_CHOOSER_RES_MAX = "inputstream.adaptive.chooser_resolution_max";
constexpr std::string_view PROP_CHOOSER_RES_SECURE_MAX = "inputstream.adaptive.chooser_resolution_secure_max";
// clang-format on
} // unnamed namespace

KodiProperties UTILS::PROPERTIES::ParseKodiProperties(
    const std::map<std::string, std::string> properties)
{
  KodiProperties props;

  for (auto& prop : properties)
  {
    bool logPropValRedacted{false};

    if (prop.first == PROP_LICENSE_TYPE)
    {
      props.m_licenseType = prop.second;
    }
    else if (prop.first == PROP_LICENSE_KEY)
    {
      props.m_licenseKey = prop.second;
      logPropValRedacted = true;
    }
    else if (prop.first == PROP_LICENSE_DATA)
    {
      props.m_licenseData = prop.second;
      logPropValRedacted = true;
    }
    else if (prop.first == PROP_LICENSE_FLAGS)
    {
      if (prop.second.find("persistent_storage") != std::string::npos)
        props.m_isLicensePersistentStorage = true;
      if (prop.second.find("force_secure_decoder") != std::string::npos)
        props.m_isLicenseForceSecureDecoder = true;
    }
    else if (prop.first == PROP_SERVER_CERT)
    {
      props.m_serverCertificate = prop.second;
      logPropValRedacted = true;
    }
    else if (prop.first == PROP_MANIFEST_TYPE)
    {
      if (StringUtils::CompareNoCase(prop.second, "MPD") == 0)
        props.m_manifestType = ManifestType::MPD;
      else if (StringUtils::CompareNoCase(prop.second, "ISM") == 0)
        props.m_manifestType = ManifestType::ISM;
      else if (StringUtils::CompareNoCase(prop.second, "HLS") == 0)
        props.m_manifestType = ManifestType::HLS;
      else
        LOG::LogF(LOGERROR, "Manifest type \"%s\" is not supported", prop.second.c_str());
    }
    else if (prop.first == PROP_MANIFEST_UPD_PARAM)
    {
      props.m_manifestUpdateParam = prop.second;
    }
    else if (prop.first == PROP_STREAM_HEADERS)
    {
      ParseHeaderString(props.m_streamHeaders, prop.second);
    }
    else if (prop.first == PROP_AUDIO_LANG_ORIG)
    {
      props.m_audioLanguageOrig = prop.second;
    }
    else if (prop.first == PROP_BANDWIDTH_MAX) //! @todo: deprecated, to be removed on next Kodi release
    {
      LOG::Log(LOGWARNING, "Warning \"inputstream.adaptive.max_bandwidth\" property is deprecated "
                           "and may not works. Please read \"Integration\" and \"Stream selection types\" "
                           "pages on the Wiki to learn more about the new properties.");
      props.m_chooserProps.m_bandwidthMax = static_cast<uint32_t>(std::stoi(prop.second));
    }
    else if (prop.first == PROP_PLAY_TIMESHIFT_BUFFER)
    {
      props.m_playTimeshiftBuffer = StringUtils::CompareNoCase(prop.second, "true") == 0;
    }
    else if (prop.first == PROP_PRE_INIT_DATA)
    {
      props.m_drmPreInitData = prop.second;
      logPropValRedacted = true;
    }
    else if (prop.first == PROP_STREAM_SELECTION_TYPE)
    {
      props.m_drmPreInitData = prop.second;
    }
    else if (prop.first == PROP_CHOOSER_BANDWIDTH_MAX)
    {
      props.m_chooserProps.m_bandwidthMax = static_cast<uint32_t>(std::stoi(prop.second));
    }
    else if (prop.first == PROP_CHOOSER_RES_MAX)
    {
      std::pair<int, int> res;
      if (SETTINGS::ParseResolutionLimit(prop.second, res))
        props.m_chooserProps.m_resolutionMax = res;
      else
        LOG::Log(LOGERROR, "Resolution not valid on \"%s\" property.", prop.first.c_str());
    }
    else if (prop.first == PROP_CHOOSER_RES_SECURE_MAX)
    {
      std::pair<int, int> res;
      if (SETTINGS::ParseResolutionLimit(prop.second, res))
        props.m_chooserProps.m_resolutionSecureMax = res;
      else
        LOG::Log(LOGERROR, "Resolution not valid on \"%s\" property.", prop.first.c_str());
    }
    else
    {
      LOG::Log(LOGWARNING, "Property found \"%s\" is not supported", prop.first.c_str());
      continue;
    }

    LOG::Log(LOGDEBUG, "Property found \"%s\" value: %s", prop.first.c_str(),
             logPropValRedacted ? "[redacted]" : prop.second.c_str());
  }

  return props;
}
