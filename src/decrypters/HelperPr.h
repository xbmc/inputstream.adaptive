/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace DRM
{

// \brief DRM PlayReady header protection parser
class PRHeaderParser
{
public:
  enum class EncryptionType
  {
    UNKNOWN,
    AESCTR, // cenc
    AESCBC, // cbcs
  };

  /*!
   * \brief Parse PlayReady header data.
   * \param prHeader The PlayReady header data as base64 string
   * \return True if parsed with success, otherwise false
   */
  bool Parse(std::string_view prHeaderBase64);

  bool Parse(const std::vector<uint8_t>& prHeader);

  /*!
   * \brief Determines if there is PlayReady protection
   * \return True if there is PlayReady protection, otherwise false
   */
  bool HasProtection() const { return !m_initData.empty(); }

  /*!
   * \brief Get keyid as 16 bytes format (converted for Widevine DRM)
   */
  const std::vector<uint8_t>& GetKID() const { return m_KID; }
  EncryptionType GetEncryption() const { return m_encryption; }
  std::string_view GetLicenseURL() const { return m_licenseURL; }
  const std::vector<uint8_t>& GetInitData() const { return m_initData; }

private:
  std::vector<uint8_t> m_KID;
  EncryptionType m_encryption{EncryptionType::UNKNOWN};
  std::string m_licenseURL;
  std::vector<uint8_t> m_initData;
};

/*!
  * \brief Fix the PlayReady header.
  *        If the LA_URL tag is missing, injects a mock LA_URL value to avoid causing the CDM to throw when creating the key request.
  *        The LA_URL attribute is optional but some Android PlayReady implementations are known to require it.
  *        Check if on v4.0.0.0 the CHECKSUM tag is missing, this is required, if so convert to v4.1.0.0 that dont need it.
  */
std::vector<uint8_t> FixPrHeader(const std::vector<uint8_t>& prHeader);

} // namespace DRM
