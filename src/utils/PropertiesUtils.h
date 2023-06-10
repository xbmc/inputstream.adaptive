/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace UTILS
{
namespace PROPERTIES
{

enum class ManifestType
{
  UNKNOWN = 0,
  MPD,
  ISM,
  HLS
};

struct ChooserProps
{
  uint32_t m_bandwidthMax{0};
  std::pair<int, int> m_resolutionMax; // Res. limit for non-protected videos
  std::pair<int, int> m_resolutionSecureMax; // Res. limit for DRM protected videos
};

struct KodiProperties
{
  std::string m_licenseType;
  std::string m_licenseKey;
  std::string m_licenseData;
  bool m_isLicensePersistentStorage{false};
  bool m_isLicenseForceSecureDecoder{false};
  std::string m_serverCertificate;
  ManifestType m_manifestType{ManifestType::UNKNOWN};
  // Can be used to force enable manifest updates,
  // and optionally to set a specific url parameter
  std::string m_manifestUpdateParam;
  // HTTP parameters used to download manifests
  std::string m_manifestParams;
  // HTTP headers used to download manifests
  std::map<std::string, std::string> m_manifestHeaders;
  // HTTP parameters used to download streams
  std::string m_streamParams;
  // HTTP headers used to download streams
  std::map<std::string, std::string> m_streamHeaders;

  // Defines what type of audio tracks should be preferred for the "default" flag,
  // accepted values are: original, impaired, or empty string.
  // When empty: it try to set the flag to a regular language track or fallback to original language
  std::string m_audioPrefType;
  // Defines if stereo audio tracks are preferred over multichannels one,
  // it depends from m_audioLangDefault
  bool m_audioPrefStereo{false};
  // Force audio streams with the specified language code to have the "default" flag
  std::string m_audioLangDefault;
  // Force audio streams with the specified language code to have the "original" flag
  std::string m_audioLangOriginal;
  // Force subtitle streams with the specified language code to have the "default" flag
  std::string m_subtitleLangDefault;

  bool m_playTimeshiftBuffer{false};
  // Set a custom delay from live edge in seconds
  uint64_t m_liveDelay{0};
  // PSSH/KID used to "pre-initialize" the DRM, the property value must be as
  // "{PSSH as base64}|{KID as base64}". The challenge/session ID data generated
  // by the initialisation of the DRM will be attached to the manifest request
  // callback as HTTP headers with the names of "challengeB64" and "sessionId"
  std::string m_drmPreInitData;
  // Define the representation chooser type to be used, to override add-on user settings
  std::string m_streamSelectionType;
  // Representation chooser properties, to override add-on user settings
  ChooserProps m_chooserProps;
};

KodiProperties ParseKodiProperties(const std::map<std::string, std::string> properties);

} // namespace PROPERTIES
} // namespace UTILS
