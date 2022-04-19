/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <map>
#include <string_view>
#include <utility>

namespace UTILS
{
namespace SETTINGS
{

const std::map<std::string_view, std::pair<int, int>> RESOLUTION_LIMITS{
    {"480p", {640, 480}}, {"640p", {960, 640}},    {"720p", {1280, 720}}, {"1080p", {1920, 1080}},
    {"2K", {2048, 1080}}, {"1440p", {2560, 1440}}, {"4K", {3840, 2160}}};

enum class StreamSelection
{
  AUTO = 0,
  MANUAL,
  MANUAL_VIDEO_ONLY
};

/*!
 * \brief Parse resolution limit as string to Width / Height pair
 * \param resStr The resolution string
 * \param res [OUT] The resolution parsed
 * \return True if has success, otherwise false
 */
bool ParseResolutionLimit(std::string_view resStr, std::pair<int, int>& res);

} // namespace SETTINGS
} // namespace UTILS
