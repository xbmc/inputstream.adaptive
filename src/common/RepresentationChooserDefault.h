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
#include <deque>
#include <optional>

namespace CHOOSER
{

class ATTR_DLL_LOCAL CRepresentationChooserDefault : public IRepresentationChooser
{
public:
  CRepresentationChooserDefault();
  ~CRepresentationChooserDefault() override {}

  virtual void Initialize(const UTILS::PROPERTIES::KodiProperties& kodiProps) override;
  virtual void PostInit() override;

  void SetDownloadSpeed(const double speed) override;

  void AddDecrypterCaps(const SSD::SSD_DECRYPTER::SSD_CAPS& ssdCaps) override;

  adaptive::AdaptiveTree::Representation* ChooseRepresentation(
      adaptive::AdaptiveTree::AdaptationSet* adp) override;

  adaptive::AdaptiveTree::Representation* ChooseNextRepresentation(
      adaptive::AdaptiveTree::AdaptationSet* adp,
      adaptive::AdaptiveTree::Representation* currentRep) override;

protected:
  /*!
   * \brief Refresh screen resolution values from the current
   */
  void RefreshResolution();

  int m_screenWidth{0};
  int m_screenHeight{0};
  std::optional<std::chrono::steady_clock::time_point> m_screenResLastUpdate;

  bool m_isHdcpOverride{false};

  std::string m_screenWidthMax; // Max resolution for non-protected video content
  std::string m_screenWidthMaxSecure; // Max resolution for protected video content

  // Ignore screen resolution, from playback starts and when it changes while playing
  bool m_ignoreScreenRes{false};
  // Ignore resolution change, while it is playing only
  bool m_ignoreScreenResChange{false};

  // The bandwidth (bit/s) calculated by the average download speed
  uint32_t m_bandwidthCurrent{0};
  uint32_t m_bandwidthMin{0};
  uint32_t m_bandwidthMax{0};

  // If true the initial bandwidth will be determined from the manifest download
  bool m_bandwidthInitAuto{false};
  // Default initial bandwidth
  uint32_t m_bandwidthInit{0};

  std::deque<double> m_downloadSpeedChron;

  std::vector<SSD::SSD_DECRYPTER::SSD_CAPS> m_decrypterCaps;
};

} // namespace CHOOSER
