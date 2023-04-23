/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <string>

#ifdef INPUTSTREAM_TEST_BUILD
#include "../test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

namespace adaptive
{
// \brief DRM PlayReady header protection parser
class ATTR_DLL_LOCAL PRProtectionParser
{
public:
  PRProtectionParser() {}
  ~PRProtectionParser() {}

  /*!
   * \brief Parse PlayReady header data.
   * \param prHeader The PlayReady header data as base64 string
   * \return True if parsed with success, otherwise false
   */
  bool ParseHeader(std::string_view prHeader);

  /*!
   * \brief Determines if there is PlayReady protection
   * \return True if there is PlayReady protection, otherwise false
   */
  bool HasProtection() const { return !m_PSSH.empty(); }

  std::string_view GetKID() const { return m_KID; }
  std::string_view GetLicenseURL() const { return m_licenseURL; }
  std::string_view GetPSSH() const { return m_PSSH; }

private:
  std::string m_KID;
  std::string m_licenseURL;
  std::string m_PSSH;
};

} // namespace adaptive
