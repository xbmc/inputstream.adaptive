/*
 *  Copyright (C) 2023 Team Kodi
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
rapidjson::Value* GetValueAtPath(rapidjson::Value& node, const std::string& path);

} // namespace JSON
} // namespace UTILS
