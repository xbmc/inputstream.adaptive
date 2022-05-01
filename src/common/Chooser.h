/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../utils/PropertiesUtils.h"
#include "../utils/SettingsUtils.h"
#include "AdaptiveTree.h"

#include <map>
#include <string_view>
#include <utility>

namespace CHOOSER
{
/*!
 * \brief Defines the behaviours on which the quality of streams is chosen
 */
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
  virtual void Initialize(const UTILS::PROPERTIES::ChooserProps& props) {}

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
   * \brief Get the representation from an adaptation set
   * \param adp The adaptation set where choose the representation
   * \return The representation
   */
  adaptive::AdaptiveTree::Representation* GetRepresentation(
      adaptive::AdaptiveTree::AdaptationSet* adp)
  {
    return GetNextRepresentation(adp, nullptr);
  }

  /*!
   * \brief Get the next representation from an adaptation set
   * \param adp The adaptation set where choose the representation
   * \param currentRep The current representation,
   *        or nullptr for first start or changed to new period
   * \return The next representation
   */
  virtual adaptive::AdaptiveTree::Representation* GetNextRepresentation(
      adaptive::AdaptiveTree::AdaptationSet* adp,
      adaptive::AdaptiveTree::Representation* currentRep) = 0;

protected:
  /*!
   * \brief Prints details of the selected or changed representation in the log
   */
  void LogDetails(adaptive::AdaptiveTree::Representation* currentRep,
                  adaptive::AdaptiveTree::Representation* nextRep);

  bool m_isSecureSession{false};

  // Current screen width resolution (this value is auto-updated by Kodi)
  int m_screenCurrentWidth{0};
  // Current screen height resolution (this value is auto-updated by Kodi)
  int m_screenCurrentHeight{0};
};

IRepresentationChooser* CreateRepresentationChooser(
    const UTILS::PROPERTIES::KodiProperties& kodiProps);

} // namespace CHOOSER
