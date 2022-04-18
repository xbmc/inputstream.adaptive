/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../SSD_dll.h"
#include "../utils/PropertiesUtils.h"
#include "../utils/SettingsUtils.h"
#include "AdaptiveTree.h"

#include <string_view>

namespace CHOOSER
{

const std::map<std::string_view, std::pair<int, int>> RESOLUTION_LIMITS{
    {"480p", {640, 480}}, {"640p", {960, 640}},    {"720p", {1280, 720}}, {"1080p", {1920, 1080}},
    {"2K", {2048, 1080}}, {"1440p", {2560, 1440}}, {"4K", {3840, 2160}}};

class ATTR_DLL_LOCAL IRepresentationChooser
{
public:
  virtual ~IRepresentationChooser() {}

  /*!
   * \brief Initialize the representation chooser.
   *        (Variables like current screen resolution and
   *        DRM data can be read only with PostInit callback)
   * \param m_kodiProps The Kodi properties
   */
  virtual void Initialize(const UTILS::PROPERTIES::KodiProperties& kodiProps) {}

  /*!
   * \brief Post initialization, will be called just after DRM initialization
   */
  virtual void PostInit() {}

  /*!
   * \brief Set the current screen resolution.
   *        To be called every time the screen resolution change.
   * \param width Width resolution
   * \param height Height resolution
   */
  void SetScreenResolution(const int width, const int height);

  /*!
   * \brief Set the current download speed.
   *        To be called at each segment download.
   * \param speed The speed in byte/s
   */
  virtual void SetDownloadSpeed(const double speed) {}

  /*!
   * \brief Get the stream selection mode. Determine whether to provide the user
   *        with the ability to choose a/v tracks from Kodi GUI settings while
   *        in playback.
   * \return The stream selection mode
   */
  virtual UTILS::SETTINGS::StreamSelection GetStreamSelectionMode()
  {
    return UTILS::SETTINGS::StreamSelection::AUTO;
  }

  /*!
   * \brief Set if the DRM use a secure session
   * \param isSecureSession Set true if a secure session is in use
   */
  virtual void SetSecureSession(const bool isSecureSession) { m_isSecureSession = isSecureSession; }

  /*!
   * \brief Add the decrypter caps of a session. Can be used to determine the
   *        HDCP version and limit of each stream representation.
   * \param ssdCaps The SSD decripter caps of a session
   */
  virtual void AddDecrypterCaps(const SSD::SSD_DECRYPTER::SSD_CAPS& ssdCaps) {}

  virtual adaptive::AdaptiveTree::Representation* ChooseRepresentation(
      adaptive::AdaptiveTree::AdaptationSet* adp) = 0;

  virtual adaptive::AdaptiveTree::Representation* ChooseNextRepresentation(
      adaptive::AdaptiveTree::AdaptationSet* adp,
      adaptive::AdaptiveTree::Representation* currentRep) = 0;

protected:
  bool m_isSecureSession{false};

  // Current screen width resolution (this value is auto-updated by Kodi)
  int m_screenCurrentWidth{0};
  // Current screen height resolution (this value is auto-updated by Kodi)
  int m_screenCurrentHeight{0};
};

IRepresentationChooser* CreateRepresentationChooser(
    const UTILS::PROPERTIES::KodiProperties& kodiProps);

} // namespace CHOOSER
