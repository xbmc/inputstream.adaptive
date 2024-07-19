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

#include <optional>
#include <string>
#include <string_view>

namespace PLAYLIST
{
enum class ContainerType;
}

namespace PLAYLIST
{
struct ProtectionScheme
{
  std::string idUri;
  std::string value;
  std::string kid;
  std::string pssh;
  std::string licenseUrl;
};

// CCommonAttribs class provide attribute data
// of class itself or when not set of the parent class (if any).
class ATTR_DLL_LOCAL CCommonAttribs
{
public:
  CCommonAttribs(CCommonAttribs* parent = nullptr);
  virtual ~CCommonAttribs() {}

  const std::string_view GetMimeType() const;
  void SetMimeType(std::string_view mimeType) { m_mimeType = mimeType; }

  ContainerType GetContainerType() const;
  void SetContainerType(ContainerType containerType) { m_containerType = containerType; }

  int GetWidth() const;
  void SetResWidth(int width) { m_resWidth = width; }

  int GetHeight() const;
  void SetResHeight(int height) { m_resHeight = height; }

  float GetAspectRatio() const;
  void SetAspectRatio(float aspectRatio) { m_aspectRatio = aspectRatio; }

  uint32_t GetFrameRate() const;
  void SetFrameRate(uint32_t frameRate) { m_frameRate = frameRate; }

  uint32_t GetFrameRateScale() const;
  void SetFrameRateScale(uint32_t frameRateScale) { m_frameRateScale = frameRateScale; }

  uint32_t GetSampleRate() const;
  void SetSampleRate(uint32_t sampleRate) { m_sampleRate = sampleRate; }

  uint32_t GetAudioChannels() const;
  void SetAudioChannels(uint32_t audioChannels) { m_audioChannels = audioChannels; }

  bool HasProtectionSchemes() const { return !m_protSchemes.empty(); }
  std::vector<ProtectionScheme>& ProtectionSchemes() { return m_protSchemes; }

protected:
  CCommonAttribs* m_parentCommonAttributes{nullptr};
  std::string m_mimeType;
  std::optional<ContainerType> m_containerType;
  int m_resHeight{0};
  int m_resWidth{0};
  float m_aspectRatio{0};
  uint32_t m_frameRate{0};
  uint32_t m_frameRateScale{0};
  uint32_t m_sampleRate{0};
  uint32_t m_audioChannels{0};
  std::vector<ProtectionScheme> m_protSchemes;
};

} // namespace PLAYLIST
