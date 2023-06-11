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
  std::string m_manifestUpdateParam; // Deprecated
  // HTTP parameters used to download manifest updates
  // Dash manifest have the optional support of placeholder $START_NUMBER$ to allow set the segment
  // start number to a parameter e.g. ?start_seq=$START_NUMBER$ become ?start_seq=10
  std::string m_manifestUpdParams;
  // HTTP parameters used to download manifests
  std::string m_manifestParams;
  // HTTP headers used to download manifests
  std::map<std::string, std::string> m_manifestHeaders;
  // HTTP parameters used to download streams
  std::string m_streamParams;
  // HTTP headers used to download streams
  std::map<std::string, std::string> m_streamHeaders;
  std::string m_audioLanguageOrig;
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
