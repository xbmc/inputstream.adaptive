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

#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace ADP
{
namespace SETTINGS
{
// Generic conversion map from family of resolutions to a common pixel format.
// If modified, the changes should reflect XML settings and Kodi properties related to resolutions.
const std::map<std::string, std::pair<int, int>> RES_CONV_LIST{
    {"auto", {0, 0}},        {"480p", {640, 480}}, {"640p", {960, 640}},    {"720p", {1280, 720}},
    {"1080p", {1920, 1080}}, {"2K", {2048, 1080}}, {"1440p", {2560, 1440}}, {"4K", {3840, 2160}}};

enum class StreamSelMode
{
  AUTO,
  MANUAL,
  MANUAL_VIDEO // Only video streams allowed to manual selection
};

class ATTR_DLL_LOCAL CCompSettings
{
public:
  CCompSettings() = default;
  ~CCompSettings() = default;

  bool IsHdcpOverride() const;

  // Chooser's settings

  StreamSelMode GetStreamSelMode() const;

  std::string GetChooserType() const;

  std::pair<int, int> GetResMax() const;
  std::pair<int, int> GetResSecureMax() const;

  bool IsBandwidthInitAuto() const;
  uint32_t GetBandwidthInit() const;
  uint32_t GetBandwidthMin() const;
  uint32_t GetBandwidthMax() const;

  bool IsIgnoreScreenRes() const;
  bool IsIgnoreScreenResChange() const;

  std::string GetChooserTestMode() const;
  int GetChooserTestSegs() const;

  // Expert settings

  int GetMediaType() const;

  bool IsDisableSecureDecoder() const;
  std::string GetDecrypterPath() const; // Widevine decrypter binary path

  bool IsDebugLicense() const;
  bool IsDebugManifest() const;
  bool IsDebugVerbose() const;
};

} // namespace SETTINGS
} // namespace ADP
