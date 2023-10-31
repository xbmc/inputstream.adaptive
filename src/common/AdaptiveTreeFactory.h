/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "utils/CurlUtils.h"

// forward
namespace adaptive
{
enum class TreeType;
class AdaptiveTree;
}

namespace PLAYLIST_FACTORY
{
/*!
 * \brief Create the adaptive tree.
 * \param kodiProps The kodi properties
 * \param manifestResp The HTTP manifest response data
 * \return The adaptive tree pointer, or nullptr when the manifest cannot be identified
 */
adaptive::AdaptiveTree* CreateAdaptiveTree(const UTILS::CURL::HTTPResponse& manifestResp);

/*!
 * \brief Try detect the manifest type based on data provided.
 * \param url The manifest effective url
 * \param contentType If any, the mime type provided in the HTTP manifest response
 * \param data The manifest data
 * \return The manifest type
 */
adaptive::TreeType InferManifestType(std::string_view url,
                                     std::string_view contentType,
                                     std::string_view data);

} // namespace PLAYLIST_FACTORY
