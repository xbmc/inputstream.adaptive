/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <string>

#include <rapidjson/document.h>

namespace UTILS
{
namespace JSON
{

/*!
 * \brief Get value from a JSON path e.g. "a/b/c"
 * \param node The json object where get the value
 * \param path The path where the value is contained
 * \return The json object if found, otherwise nullptr.
 */
const rapidjson::Value* GetValueAtPath(const rapidjson::Value& node, const std::string& path);

/*!
 * \brief Get value from an unknown JSON path,
 *        then traverse all even nested dictionaries to search for the specified key name.
 * \param node The json object where find the path/value
 * \param keyName The key name to search for
 * \return The json object if found, otherwise nullptr.
 */
const rapidjson::Value* GetValueTraversePaths(const rapidjson::Value& node,
                                              const std::string& keyName);

} // namespace JSON
} // namespace UTILS
