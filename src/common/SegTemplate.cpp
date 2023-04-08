/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SegTemplate.h"

#include "kodi/tools/StringUtils.h"

using namespace PLAYLIST;
using namespace kodi::tools;

PLAYLIST::CSegmentTemplate::CSegmentTemplate(CSegmentTemplate* parent /* = nullptr */)
{
  m_parentSegTemplate = parent;
}

std::string_view PLAYLIST::CSegmentTemplate::GetInitialization() const
{
  if (!m_initialization.empty())
    return m_initialization;

  if (m_parentSegTemplate)
    return m_parentSegTemplate->GetInitialization();

  return ""; // Default value
}

std::string_view PLAYLIST::CSegmentTemplate::GetMedia() const
{
  if (!m_media.empty())
    return m_media;

  if (m_parentSegTemplate)
    return m_parentSegTemplate->GetMedia();

  return ""; // Default value
}

std::string_view PLAYLIST::CSegmentTemplate::GetMediaUrl() const
{
  if (!m_mediaUrl.empty())
    return m_mediaUrl;

  if (m_parentSegTemplate)
    return m_parentSegTemplate->GetMediaUrl();

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
