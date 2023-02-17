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

namespace UTILS
{
namespace FILESYS
{

/*!
 * \brief Save the data into a file
 * \param filePath The file path where to save the file, if the path is missing will be created.
 * \param data The data to be saved.
 * \param overwrite If true will overwrite the existing file.
 * \return True if success, otherwise false.
 */
bool SaveFile(std::string_view filePath, std::string_view data, bool overwrite);

/*!
 * \brief Combine a path with another one
 * \param path The starting path.
 * \param filePath The path to be combined, like filename or another path.
 * \return The combined path.
 */
std::string PathCombine(std::string path, std::string filePath);

/*!
 * \brief Get the user-related data folder of the addon.
 * \return The path.
 */
std::string GetAddonUserPath();

/*!
 * \brief Check for duplicates for the file path, and change the filename
 *        based on the number of duplicate files.
 * \param path [IN][OUT] The file path. based on the number of duplicate files found,
 *             the file name will be renamed by appending the duplicate count number,
 *             for example: if "test.txt" exists, the next become "text_1.txt", "text_2.txt", ...
 * \param filesLimit Maximum limit of duplicate files allowed, if 0 there is no limit.
 * \return True is successful, otherwise false if exceeds the number of duplicate files allowed.
 */
bool CheckDuplicateFilePath(std::string& filePath, uint32_t filesLimit = 0);

/*!
 * \brief Remove a directory.
 * \param path The directory to be deleted.
 * \param recursive If true all sub-folders will be deleted,
 *                  otherwise only the specified folder will be emptied.
 * \return True is success, otherwise false.
 */
bool RemoveDirectory(std::string_view path, bool recursive = true);

} // namespace FILESYS
} // namespace UTILS
