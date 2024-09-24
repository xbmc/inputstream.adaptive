/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <string>
#include <string_view>

namespace DRM
{

/*!
 * \brief Generate an hash by using the base domain of an URL.
 * \param url An URL
 * \return The hash of a base domain URL
 */
std::string GenerateUrlDomainHash(std::string_view url);

}; // namespace DRM
