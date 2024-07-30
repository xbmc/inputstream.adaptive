/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SegTemplate.h"
#include "Segment.h"
#include "utils/log.h"
#include "utils/StringUtils.h"

#include "kodi/tools/StringUtils.h"

#include <cstdio> // snprintf

using namespace PLAYLIST;
using namespace UTILS;
using namespace kodi::tools;

PLAYLIST::CSegmentTemplate::CSegmentTemplate(const std::optional<CSegmentTemplate>& other)
{
  if (other.has_value())
    *this = *other;
}

std::string PLAYLIST::CSegmentTemplate::GetInitialization() const
{
  if (!m_initialization.empty())
    return m_initialization;

  return ""; // Default value
}

std::string PLAYLIST::CSegmentTemplate::GetMedia() const
{
  if (!m_media.empty())
    return m_media;

  return ""; // Default value
}

bool PLAYLIST::CSegmentTemplate::HasMediaNumber() const
{
  return STRING::Contains(m_media, "$Number");
}

uint32_t PLAYLIST::CSegmentTemplate::GetTimescale() const
{
  if (m_timescale.has_value())
    return *m_timescale;

  return 0; // Default value
}

uint32_t PLAYLIST::CSegmentTemplate::GetDuration() const
{
  if (m_duration.has_value())
    return *m_duration;

  return 0; // Default value
}

uint64_t PLAYLIST::CSegmentTemplate::GetStartNumber() const
{
  if (m_startNumber.has_value())
    return *m_startNumber;

  return 1; // Default value
}

uint64_t PLAYLIST::CSegmentTemplate::GetEndNumber() const
{
  if (m_endNumber.has_value())
    return *m_endNumber;

  return 0; // Default value
}

uint64_t PLAYLIST::CSegmentTemplate::GetPresTimeOffset() const
{
  if (m_ptsOffset.has_value())
    return *m_ptsOffset;

  return 0; // Default value
}

CSegment PLAYLIST::CSegmentTemplate::MakeInitSegment()
{
  CSegment seg;
  seg.SetIsInitialization(true);
  seg.url = GetInitialization();
  return seg;
}

std::string PLAYLIST::CSegmentTemplate::FormatUrl(std::string_view url,
                                                  const std::string id,
                                                  const uint32_t bandwidth,
                                                  const uint64_t number,
                                                  const uint64_t time)
{
  size_t curPos{0};
  std::string ret;

  do
  {
    size_t chPos = url.find('$', curPos);
    if (chPos == std::string::npos)
    {
      // No other identifiers to substitute
      ret += url.substr(curPos);
      curPos = url.size();
      break;
    }

    ret += url.substr(curPos, chPos - curPos);

    size_t nextChPos = url.find('$', chPos + 1);

    if (nextChPos == std::string::npos)
      nextChPos = url.size();

    std::string_view identifier = url.substr(chPos, nextChPos - chPos + 1);

    if (identifier == "$$") // Escape sequence
    {
      ret += "$";
      curPos = nextChPos + 1;
    }
    else if (identifier == "$RepresentationID$")
    {
      ret += id;
      curPos = nextChPos + 1;
    }
    else if (STRING::StartsWith(identifier, "$Number"))
    {
      ret += FormatIdentifier(identifier, number);
      curPos = nextChPos + 1;
    }
    else if (STRING::StartsWith(identifier, "$Time"))
    {
      ret += FormatIdentifier(identifier, time);
      curPos = nextChPos + 1;
    }
    else if (STRING::StartsWith(identifier, "$Bandwidth"))
    {
      ret += FormatIdentifier(identifier, static_cast<uint64_t>(bandwidth));
      curPos = nextChPos + 1;
    }
    else // Unknow indentifier, or $ char that isnt part of an identifier
    {
      if (nextChPos != url.size())
        identifier.remove_suffix(1);
      ret += identifier;
      curPos = nextChPos;
    }
  } while (curPos < url.size());

  return ret;
}

std::string PLAYLIST::CSegmentTemplate::FormatIdentifier(std::string_view identifier,
                                                         const uint64_t value)
{
  if (identifier.back() == '$')
    identifier.remove_suffix(1);
  else
  {
    LOG::LogF(LOGWARNING, "Cannot format template identifier because malformed");
    return std::string{identifier};
  }

  size_t formatTagIndex = identifier.find("%0");
  std::string formatTag = "%01d"; // default format tag

  if (formatTagIndex != std::string::npos)
  {
    formatTag = identifier.substr(formatTagIndex);
    switch (formatTag.back())
    {
      case 'd':
      case 'i':
      case 'u':
      case 'x':
      case 'X':
      case 'o':
        break; // supported conversions as dash.js
      default:
        return std::string{identifier}; // leave as is
    }
  }
  // sprintf expect the right length of data type
  if (formatTag.size() > 2 &&
      (formatTag[formatTag.size() - 2] != 'l' && formatTag[formatTag.size() - 3] != 'l'))
  {
    formatTag.insert(formatTag.size() - 1, "ll");
  }

  char substitution[128];
  if (std::snprintf(substitution, 128, formatTag.c_str(), value) > 0)
    return substitution;
  else
    LOG::LogF(LOGERROR, "Cannot convert value \"%llu\" with \"%s\" format tag", value,
              formatTag.c_str());

  return std::string{identifier};
}
