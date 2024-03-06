/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

/*!
 * \brief The runtime configuration for the CDM instance
*/
struct MediaFoundationCdmConfig
{
  MediaFoundationCdmConfig(bool distinctive_identifier = false, bool persistent_state = false)
    : allow_distinctive_identifier(distinctive_identifier),
      allow_persistent_state(persistent_state),
      use_hw_secure_codecs(false)
  {
    
  }

  // Allow access to a distinctive identifier.
  bool allow_distinctive_identifier;

  // Allow access to persistent state.
  bool allow_persistent_state;

  // Use hardware-secure codecs. 
  bool use_hw_secure_codecs;
};
