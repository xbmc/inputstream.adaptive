/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "JsonUtils.h"

rapidjson::Value* UTILS::JSON::GetValueAtPath(rapidjson::Value& node, const std::string& path)
{
  size_t pos = path.find('/');
  std::string current_level = path.substr(0, pos);

  if (node.IsObject() && node.HasMember(current_level.c_str()))
  {
    if (pos == std::string::npos)
    {
      return &node[current_level.c_str()];
    }
    else
    {
      return GetValueAtPath(node[current_level.c_str()], path.substr(pos + 1));
    }
  }

  return nullptr;
}
