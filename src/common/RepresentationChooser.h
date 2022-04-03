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
#include "AdaptiveTree.h"

namespace adaptive
{

class ATTR_DLL_LOCAL IRepresentationChooser
{
public:
  IRepresentationChooser() {}
  virtual ~IRepresentationChooser() {}

  /*! \brief Initialize the representation chooser.
   *   (Variables like current screen resolution and
   *   DRM data can be read only with PostInit callback)
   *  \param m_kodiProps The Kodi properties
   */
  virtual void Initialize(const UTILS::PROPERTIES::KodiProperties& kodiProps) {}

  /*! \brief Post initialization, will be called just after DRM initialization
   */
  virtual void PostInit() {}

  /*! \brief Set the current screen resolution.
  *    To be called every time the screen resolution change.
   *  \param width Width resolution
   *  \param height Height resolution
   */
  virtual void SetScreenResolution(int width, int height) {}

  /*! \brief Set the current download speed.
   *   To be called at each segment download.
   *  \param speed The speed in byte/s
   */
  virtual void SetDownloadSpeed(double speed) {}

  /*! \brief Get the current donwload speed
   *  \return The speed in byte/s
   */
  virtual double GetDownloadSpeed() { return 0.0; }

  /*! \brief Set if the DRM use a secure session
   *  \param isSecureSession Set true if a secure session is in use
   */
  virtual void SetSecureSession(bool isSecureSession) {}

  /*! \brief Add the decrypter caps of a session. Can be used to determine the
   *   HDCP version and limit of each stream representation.
   *  \param ssdCaps The SSD decripter caps of a session
   */
  virtual void AddDecrypterCaps(const SSD::SSD_DECRYPTER::SSD_CAPS& ssdCaps) {}

  virtual AdaptiveTree::Representation* ChooseRepresentation(AdaptiveTree::AdaptationSet* adp) = 0;

  virtual AdaptiveTree::Representation* ChooseNextRepresentation(AdaptiveTree::AdaptationSet* adp,
                                                                 AdaptiveTree::Representation* rep,
                                                                 size_t& validSegmentBuffers,
                                                                 size_t& availableSegmentBuffers,
                                                                 uint32_t& bufferLengthAssured,
                                                                 uint32_t& bufferLengthMax,
                                                                 uint32_t repCounter) = 0;
};

} // namespace adaptive
