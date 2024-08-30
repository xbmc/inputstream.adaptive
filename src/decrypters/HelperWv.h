/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cstdint>
#include <vector>

namespace DRM
{

/*!
 * \brief Generate a synthesized Widevine PSSH.
 *        (WidevinePsshData as google protobuf format
 *        https://github.com/devine-dl/pywidevine/blob/master/pywidevine/license_protocol.proto)
 * \param kid The KeyId
 * \param contentIdData Custom content for the "content_id" field as bytes
 *                      Placeholders allowed:
 *                      {KID} To inject the KID as bytes
 *                      {UUID} To inject the KID as UUID string format
 * \return The pssh if has success, otherwise empty value.
 */
std::vector<uint8_t> MakeWidevinePsshData(const std::vector<std::vector<uint8_t>>& keyIds,
                                          std::vector<uint8_t> contentIdData);

/*!
 * \brief Generate a synthesized Widevine PSSH.
 *        (WidevinePsshData as google protobuf format
 *        https://github.com/devine-dl/pywidevine/blob/master/pywidevine/license_protocol.proto)
 * \param kid The KeyId
 * \param contentIdData Custom content for the "content_id" field as bytes
 *                      Placeholders allowed:
 *                      {KID} To inject the KID as bytes
 *                      {UUID} To inject the KID as UUID string format
 * \return The pssh if has success, otherwise empty value.
 */
void ParseWidevinePssh(const std::vector<uint8_t>& wvPsshData,
                       std::vector<std::vector<uint8_t>>& keyIds);

} // namespace DRM
