/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "RepresentationChooser.h"

#include <chrono>

namespace adaptive
{

class ATTR_DLL_LOCAL CRepresentationChooserDefault : public IRepresentationChooser
{
public:
  CRepresentationChooserDefault(std::string kodiProfilePath);
  ~CRepresentationChooserDefault() override;

  virtual void Initialize(const UTILS::PROPERTIES::KodiProperties& kodiProps) override;
  virtual void PostInit() override;

  void SetScreenResolution(int width, int height) override;

  void SetDownloadSpeed(double speed) override;
  double GetDownloadSpeed() override { return m_downloadCurrentSpeed; }

  void SetSecureSession(bool isSecureSession) override { m_isSecureSession = isSecureSession; }
  void AddDecrypterCaps(const SSD::SSD_DECRYPTER::SSD_CAPS& ssdCaps) override;

  AdaptiveTree::Representation* ChooseRepresentation(AdaptiveTree::AdaptationSet* adp) override;

  AdaptiveTree::Representation* ChooseNextRepresentation(AdaptiveTree::AdaptationSet* adp,
                                                         AdaptiveTree::Representation* rep,
                                                         size_t& validSegmentBuffers,
                                                         size_t& availableSegmentBuffers,
                                                         uint32_t& bufferLengthAssured,
                                                         uint32_t& bufferLengthMax,
                                                         uint32_t repCounter) override;

protected:
  int m_screenCurrentWidth{0};
  int m_screenCurrentHeight{0};
  int m_screenSelWidth{0};
  int m_screenSelHeight{0};
  int m_screenNextWidth{0};
  int m_screenNextHeight{0};

  bool m_isScreenResNeedUpdate{true};
  std::chrono::steady_clock::time_point m_screenResLastUpdate{std::chrono::steady_clock::now()};

  bool m_isSecureSession{false};
  bool m_isHdcpOverride{false};

  int m_screenWidthMax{0}; // Max resolution for non-protected video content
  int m_screenWidthMaxSecure{0}; // Max resolution for protected video content

  // Ignore screen resolution, from playback starts and when it changes while playing
  bool m_ignoreScreenRes{false};
  // Ignore resolution change, while it is playing only
  bool m_ignoreScreenResChange{false};

  uint32_t m_bandwidthStored{0};
  uint32_t m_bandwidthCurrent{0};
  uint32_t m_bandwidthMin{0};
  uint32_t m_bandwidthMax{0};

  uint32_t m_bufferDurationAssured{0};
  uint32_t m_bufferDurationMax{0};

  double m_downloadCurrentSpeed{0};
  double m_downloadAverageSpeed{0};

  std::vector<SSD::SSD_DECRYPTER::SSD_CAPS> m_decrypterCaps;
  std::string m_bandwidthFilePath;
};

} // namespace adaptive
