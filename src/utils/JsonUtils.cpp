/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "JsonUtils.h"

namespace
{
const rapidjson::Value* TraversePaths(const rapidjson::Value& node, const std::string& keyName)
{
  if (node.IsObject())
  {
    for (auto itr = node.MemberBegin(); itr != node.MemberEnd(); ++itr)
    {
      if (itr->name.GetString() == keyName)
      {
        if (itr->value.IsString() || itr->value.IsNumber())
          return &itr->value;
        else if (itr->value.IsObject() || itr->value.IsArray())
        {
          const rapidjson::Value* ret = TraversePaths(itr->value, keyName);
          if (ret)
            return ret;
        }
      }
      else if (itr->value.IsArray())
      {
        for (auto& item : itr->value.GetArray())
        {
          const rapidjson::Value* ret = TraversePaths(item, keyName);
          if (ret)
            return ret;
        }
      }
      else if (itr->value.IsObject())
      {
        const rapidjson::Value* ret = TraversePaths(itr->value, keyName);
        if (ret)
          return ret;
      }
    }
  }
  else if (node.IsArray())
  {
    for (auto& item : node.GetArray())
    {
      const rapidjson::Value* ret = TraversePaths(item, keyName);
      if (ret)
        return ret;
    }
  }

  return nullptr;
}

} // unnamed namespace

const rapidjson::Value* UTILS::JSON::GetValueAtPath(const rapidjson::Value& node,
                                                    const std::string& path)
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

const rapidjson::Value* UTILS::JSON::GetValueTraversePaths(const rapidjson::Value& node,
                                                           const std::string& keyName)
{
  return TraversePaths(node, keyName);
}
