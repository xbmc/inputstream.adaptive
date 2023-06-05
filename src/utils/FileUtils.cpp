/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "FileUtils.h"

#include "log.h"

#ifdef INPUTSTREAM_TEST_BUILD
#include "../test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#include <kodi/Filesystem.h>
#endif

#include <cctype> // isalpha

bool UTILS::FILESYS::SaveFile(std::string_view filePath, std::string_view data, bool overwrite)
{
  if (filePath.empty())
    return false;

  kodi::vfs::CFile saveFile;
  if (!saveFile.OpenFileForWrite(filePath.data(), overwrite))
  {
    LOG::LogF(LOGERROR, "Cannot create file \"%s\".", filePath.data());
    return false;
  }

  bool isWritten = saveFile.Write(data.data(), data.size()) != -1;
  saveFile.Close();
  return isWritten;
}

std::string UTILS::FILESYS::PathCombine(std::string path, std::string filePath)
{
  if (path.empty())
    return filePath;

  const char separator = path[1] == ':' && std::isalpha(path[0]) ? '\\' : '/';

  if (path.back() == separator)
    path.pop_back();

  if (filePath.front() == separator)
    filePath.erase(0, 1);

  return path + separator + filePath;
}

std::string UTILS::FILESYS::GetAddonUserPath()
{
  return kodi::addon::GetUserPath();
}

bool UTILS::FILESYS::CheckDuplicateFilePath(std::string& filePath, uint32_t filesLimit /* = 0 */)
{
  const size_t extensionPos = filePath.rfind('.');
  std::string renamedFilePath = filePath;

  for (uint32_t index = 1; kodi::vfs::FileExists(renamedFilePath, false); index++)
  {
    if (filesLimit != 0 && index > filesLimit)
    {
      LOG::LogF(LOGERROR, "The file path \"%s\" exceeds the maximum amount of duplicate files.",
                filePath.c_str());
      return false;
    }

    if (extensionPos != std::string::npos)
    {
      renamedFilePath = filePath.substr(0, extensionPos) + "_" + std::to_string(index) +
                        filePath.substr(extensionPos);
    }
    else
    {
      renamedFilePath = filePath + "_" + std::to_string(index);
    }
  }

  filePath = renamedFilePath;
  return true;
}

bool UTILS::FILESYS::RemoveDirectory(std::string_view path, bool recursive /* = true */)
{
  return kodi::vfs::RemoveDirectory(path.data(), recursive);
}

std::string UTILS::FILESYS::GetFileExtension(std::string path)
{
  size_t extPos = path.rfind('.');
  if (extPos != std::string::npos)
    return path.substr(extPos + 1);

  return {};
}
