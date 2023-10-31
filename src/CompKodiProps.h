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
#include <string>
#include <string_view>
#include <utility>

namespace ADP
{
namespace KODI_PROPS
{

enum class ManifestType // Deprecated
{
  UNKNOWN = 0,
  MPD,
  ISM,
  HLS
};

// Chooser's properties that will override XML settings
struct ChooserProps
{
  std::string m_chooserType; // Specifies chooser type to be used
  uint32_t m_bandwidthMax{0};
  std::pair<int, int> m_resolutionMax; // Res. limit for non-protected videos (values 0 means auto)
  std::pair<int, int> m_resolutionSecureMax; // Res. limit for DRM protected videos (values 0 means auto)
};

class ATTR_DLL_LOCAL CCompKodiProps
{
public:
  CCompKodiProps(const std::map<std::string, std::string>& props);
  ~CCompKodiProps() = default;

  std::string_view GetLicenseType() const { return m_licenseType; }
  std::string_view GetLicenseKey() const { return m_licenseKey; }
  // \brief Get custom PSSH initialization license data
  std::string_view GetLicenseData() const { return m_licenseData; }

  bool IsLicensePersistentStorage() const { return m_isLicensePersistentStorage; }
  bool IsLicenseForceSecDecoder() const { return m_isLicenseForceSecureDecoder; }

  std::string_view GetServerCertificate() const { return m_serverCertificate; }
  ManifestType GetManifestType() const { return m_manifestType; } // Deprecated

  std::string GetManifestUpdParam() const { return m_manifestUpdateParam; } // Deprecated
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
  // \brief Get a custom delay from LIVE edge in seconds
  uint64_t GetLiveDelay() const { return m_liveDelay; }

  /*
   * \brief Get data to "pre-initialize" the DRM, if set is represented as a string
   *        of base64 data splitted by a pipe char as: "{PSSH as base64}|{KID as base64}".
   */
  std::string_view GetDrmPreInitData() const { return m_drmPreInitData; }

  // \brief Specifies the chooser properties that will override XML settings
  const ChooserProps& GetChooserProps() const { return m_chooserProps; }

private:
  std::string m_licenseType;
  std::string m_licenseKey;
  std::string m_licenseData;
  bool m_isLicensePersistentStorage{false};
  bool m_isLicenseForceSecureDecoder{false};
  std::string m_serverCertificate;
  ManifestType m_manifestType{ManifestType::UNKNOWN}; // Deprecated
  std::string m_manifestUpdateParam; // Deprecated
  std::string m_manifestUpdParams;
  std::string m_manifestParams;
  std::map<std::string, std::string> m_manifestHeaders;
  std::string m_streamParams;
  std::map<std::string, std::string> m_streamHeaders;
  std::string m_audioLanguageOrig;
  bool m_playTimeshiftBuffer{false};
  uint64_t m_liveDelay{0};
  std::string m_drmPreInitData;
  ChooserProps m_chooserProps;
};

} // namespace KODI_PROPS
} // namespace ADP
