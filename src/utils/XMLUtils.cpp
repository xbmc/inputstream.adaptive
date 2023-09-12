/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "XMLUtils.h"

#include "oscompat.h" // _mkgmtime
#include "StringUtils.h"
#include "kodi/tools/StringUtils.h"
#include "log.h"
#include "pugixml.hpp"

#include <cstdio> // sscanf
#include <regex>

using namespace UTILS::XML;
using namespace kodi::tools;
using namespace pugi;

uint64_t UTILS::XML::ParseDate(std::string_view timeStr,
                               uint64_t fallback /* = std::numeric_limits<uint64_t>::max() */)
{
  int year, mon, day, hour, minu, sec;
  if (std::sscanf(timeStr.data(), "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &hour, &minu, &sec) == 6)
  {
    tm tmd{0};
    tmd.tm_year = year - 1900;
    tmd.tm_mon = mon - 1;
    tmd.tm_mday = day;
    tmd.tm_hour = hour;
    tmd.tm_min = minu;
    tmd.tm_sec = sec;
    return static_cast<uint64_t>(_mkgmtime(&tmd));
  }
  return fallback;
}

double UTILS::XML::ParseDuration(std::string_view durationStr)
{
  static const std::regex pattern("^P(?:([0-9]*)Y)?(?:([0-9]*)M)?(?:([0-9]*)D)"
                                  "?(?:T(?:([0-9]*)H)?(?:([0-9]*)M)?(?:([0-9.]*)S)?)?$");

  if (durationStr.empty())
    return 0;

  std::cmatch matches;
  std::regex_match(durationStr.data(), matches, pattern);

  if (matches.size() == 0)
  {
    LOG::LogF(LOGWARNING, "Duration string \"%s\" is not valid.", durationStr);
    return 0;
  }

  double years = STRING::ToDouble(matches[1].str());
  double months = STRING::ToDouble(matches[2].str());
  double days = STRING::ToDouble(matches[3].str());
  double hours = STRING::ToDouble(matches[4].str());
  double minutes = STRING::ToDouble(matches[5].str());
  double seconds = STRING::ToDouble(matches[6].str());

  // Assume a year always has 365 days and a month always has 30 days.
  return years * (60 * 60 * 24 * 365) + months * (60 * 60 * 24 * 30) + days * (60 * 60 * 24) +
         hours * (60 * 60) + minutes * 60 + seconds;
}

size_t UTILS::XML::CountChilds(pugi::xml_node node, std::string_view childTagName /* = "" */)
{
  size_t count{0};
  for (xml_node nodeChild : node.children(childTagName.data()))
  {
    count++;
  }
  return count;
}

xml_attribute UTILS::XML::FirstAttributeNoPrefix(pugi::xml_node node,
                                                 std::string_view attributeName)
{
  for (xml_attribute attr : node.attributes())
  {
    std::string_view currentAttribName = attr.name();
    size_t delimiterPos = currentAttribName.find(':');
    if (delimiterPos == std::string::npos)
      continue;

    currentAttribName.remove_prefix(delimiterPos + 1);
    if (currentAttribName != attributeName)
      continue;

    return attr;
  }

  return xml_attribute();
}

std::string_view UTILS::XML::GetAttrib(pugi::xml_node& node,
                                       std::string_view name,
                                       std::string_view defaultValue /* = "" */)
{
  return node.attribute(name.data()).as_string(defaultValue.data());
}

int UTILS::XML::GetAttribInt(pugi::xml_node& node,
                             std::string_view name,
                             int defaultValue /* = 0 */)
{
  return node.attribute(name.data()).as_int(defaultValue);
}

uint32_t UTILS::XML::GetAttribUint32(pugi::xml_node& node,
                                     std::string_view name,
                                     uint32_t defaultValue /* = 0 */)
{
  return node.attribute(name.data()).as_uint(defaultValue);
}

uint64_t UTILS::XML::GetAttribUint64(pugi::xml_node& node,
                                     std::string_view name,
                                     uint64_t defaultValue)
{
  return node.attribute(name.data()).as_ullong(defaultValue);
}

bool UTILS::XML::QueryAttrib(pugi::xml_node& node, std::string_view name, std::string& value)
{
  xml_attribute attrib = node.attribute(name.data());
  if (attrib)
  {
    value = attrib.as_string();
    return true;
  }
  return false;
}

bool UTILS::XML::QueryAttrib(pugi::xml_node& node, std::string_view name, int& value)
{
  xml_attribute attrib = node.attribute(name.data());
  if (attrib)
  {
    value = attrib.as_int();
    return true;
  }
  return false;
}

bool UTILS::XML::QueryAttrib(pugi::xml_node& node, std::string_view name, uint32_t& value)
{
  xml_attribute attrib = node.attribute(name.data());
  if (attrib)
  {
    value = attrib.as_uint();
    return true;
  }
  return false;
}

bool UTILS::XML::QueryAttrib(pugi::xml_node& node, std::string_view name, uint64_t& value)
{
  xml_attribute attrib = node.attribute(name.data());
  if (attrib)
  {
    value = attrib.as_ullong();
    return true;
  }
  return false;
}
