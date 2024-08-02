/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <string>
#include <vector>

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
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

  /*!
   * \brief Get keyid as 16 bytes format (converted for Widevine DRM)
   */
  const std::vector<uint8_t>& GetKID() const { return m_KID; }
  std::string_view GetLicenseURL() const { return m_licenseURL; }
  const std::vector<uint8_t>& GetPSSH() const { return m_PSSH; }

private:
  std::vector<uint8_t> m_KID;
  std::string m_licenseURL;
  std::vector<uint8_t> m_PSSH;
};

// \brief Parse PSSH data format (ref. https://w3c.github.io/encrypted-media/format-registry/initdata/cenc.html#common-system)
class ATTR_DLL_LOCAL CPsshParser
{
public:
  bool Parse(const std::vector<uint8_t>& data);

  const std::vector<uint8_t>& GetSystemId() const { return m_systemId; }

  /*!
   * \brief Get keyid's as 16 bytes format
   */
  const std::vector<std::vector<uint8_t>>& GetKeyIds() const { return m_keyIds; }
  const std::vector<uint8_t>& GetData() const { return m_data; }

private:
  const uint8_t m_boxTypePssh[4]{'p', 's', 's', 'h'};
  uint8_t m_version{0};
  uint32_t m_flags{0};
  std::vector<uint8_t> m_systemId;
  std::vector<std::vector<uint8_t>> m_keyIds;
  std::vector<uint8_t> m_data;
};

} // namespace adaptive
