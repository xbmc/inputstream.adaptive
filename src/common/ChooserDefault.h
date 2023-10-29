/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "Chooser.h"

#include <chrono>
#include <deque>
#include <optional>
#include <utility>

namespace CHOOSER
{
/*!
 * \brief Adaptive stream, the quality of the stream is changed according
 *        to the bandwidth and screen resolution
 */
class ATTR_DLL_LOCAL CRepresentationChooserDefault : public IRepresentationChooser
{
public:
  CRepresentationChooserDefault();
  ~CRepresentationChooserDefault() override {}

  virtual void Initialize(const ADP::KODI_PROPS::ChooserProps& props) override;
  virtual void SetSecureSession(const bool isSecureSession) override;
  virtual void PostInit() override;

  void SetDownloadSpeed(const double speed) override;

  PLAYLIST::CRepresentation* GetNextRepresentation(PLAYLIST::CAdaptationSet* adp,
                                                   PLAYLIST::CRepresentation* currentRep) override;

protected:
  /*!
   * \brief Check if screen resolution is changed, if so will refresh values
   */
  void CheckResolution();

  /*!
   * \brief Refresh screen resolution values from the current
   */
  void RefreshResolution();

  int m_screenWidth{0};
  int m_screenHeight{0};
  std::optional<std::chrono::steady_clock::time_point> m_screenResLastUpdate;

  std::pair<int, int> m_screenResMax; // Max resolution for non-protected video content
  std::pair<int, int> m_screenResSecureMax; // Max resolution for protected video content

  // Ignore screen resolution, from playback starts and when it changes while playing
  bool m_ignoreScreenRes{false};
  // Ignore resolution change, while it is playing only
  bool m_ignoreScreenResChange{false};

  // The bandwidth (bit/s) calculated by the average download speed
  uint32_t m_bandwidthCurrent{0};
  // The average bandwidth (bit/s) that could be limited by user settings or add-on
  uint32_t m_bandwidthCurrentLimited{0};
  uint32_t m_bandwidthMin{0};
  uint32_t m_bandwidthMax{0};

  // If true the initial bandwidth will be determined from the manifest download
  bool m_bandwidthInitAuto{false};
  // Default initial bandwidth
  uint32_t m_bandwidthInit{0};

  std::deque<double> m_downloadSpeedChron;
};

} // namespace CHOOSER
