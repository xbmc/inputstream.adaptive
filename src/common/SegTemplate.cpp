/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SegTemplate.h"
#include "Segment.h"
#include "../utils/log.h"

#include "kodi/tools/StringUtils.h"

#include <cstdio> // sprintf

using namespace PLAYLIST;
using namespace kodi::tools;

PLAYLIST::CSegmentTemplate::CSegmentTemplate(CSegmentTemplate* parent /* = nullptr */)
{
  m_parentSegTemplate = parent;
}

std::string PLAYLIST::CSegmentTemplate::GetInitialization() const
{
  if (!m_initialization.empty())
    return m_initialization;

  if (m_parentSegTemplate)
    return m_parentSegTemplate->GetInitialization();

  return ""; // Default value
}

std::string PLAYLIST::CSegmentTemplate::GetMedia() const
{
  if (!m_media.empty())
    return m_media;

  if (m_parentSegTemplate)
    return m_parentSegTemplate->GetMedia();

  return ""; // Default value
}

uint32_t PLAYLIST::CSegmentTemplate::GetTimescale() const
{
  if (m_timescale.has_value())
    return *m_timescale;

  if (m_parentSegTemplate)
    return m_parentSegTemplate->GetTimescale();

  return 0; // Default value
}

uint32_t PLAYLIST::CSegmentTemplate::GetDuration() const
{
  if (m_duration.has_value())
    return *m_duration;

  if (m_parentSegTemplate)
    return m_parentSegTemplate->GetDuration();

  return 0; // Default value
}

uint64_t PLAYLIST::CSegmentTemplate::GetStartNumber() const
{
  if (m_startNumber.has_value())
    return *m_startNumber;

  if (m_parentSegTemplate)
    return m_parentSegTemplate->GetStartNumber();

  return 1; // Default value
}

bool PLAYLIST::CSegmentTemplate::HasVariableTime() const
{
  return m_media.find("$Time") != std::string::npos;
}

CSegment PLAYLIST::CSegmentTemplate::MakeInitSegment()
{
  CSegment seg;
  seg.SetIsInitialization(true);
  seg.url = GetInitialization();
  return seg;
}

std::string PLAYLIST::CSegmentTemplate::FormatUrl(const std::string url,
                                                  const std::string id,
                                                  const uint32_t bandwidth,
                                                  const uint64_t number,
                                                  const uint64_t time)
{
  std::vector<std::string> chunks = StringUtils::Split(url, '$');

  if (chunks.size() > 1)
  {
    for (size_t i = 0; i < chunks.size(); i++)
    {
      if (chunks.at(i) == "RepresentationID")
        chunks.at(i) = id;
      else if (chunks.at(i).find("Bandwidth") == 0)
        FormatIdentifier(chunks.at(i), static_cast<uint64_t>(bandwidth));
      else if (chunks.at(i).find("Number") == 0)
        FormatIdentifier(chunks.at(i), number);
      else if (chunks.at(i).find("Time") == 0)
        FormatIdentifier(chunks.at(i), time);
    }

    std::string replacedUrl = "";
    for (size_t i = 0; i < chunks.size(); i++)
    {
      replacedUrl += chunks.at(i);
    }
    return replacedUrl;
  }
  else
  {
    return url;
  }
}

void PLAYLIST::CSegmentTemplate::FormatIdentifier(std::string& identifier, const uint64_t value)
{
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
        return; // leave as is
    }
  }
  // sprintf expect the right length of data type
  if (formatTag.size() > 2 &&
      (formatTag[formatTag.size() - 2] != 'l' && formatTag[formatTag.size() - 3] != 'l'))
  {
    formatTag.insert(formatTag.size() - 1, "ll");
  }

  char substitution[128];
  if (std::sprintf(substitution, formatTag.c_str(), value) > 0)
    identifier = substitution;
  else
    LOG::LogF(LOGERROR, "Cannot convert value \"%llu\" with \"%s\" format tag", value,
              formatTag.c_str());
}
