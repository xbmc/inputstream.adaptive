/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CommonAttribs.h"

#include "../utils/StringUtils.h"
#include "Period.h"

using namespace PLAYLIST;
using namespace UTILS;

PLAYLIST::CCommonAttribs::CCommonAttribs(CCommonAttribs* parent /* = nullptr */)
{
  m_parentCommonAttributes = parent;
}

const std::string_view PLAYLIST::CCommonAttribs::GetMimeType() const
{
  if (!m_mimeType.empty() || !m_parentCommonAttributes)
    return m_mimeType;
  return m_parentCommonAttributes->GetMimeType();
}

ContainerType PLAYLIST::CCommonAttribs::GetContainerType() const
{
  if (m_containerType.has_value())
    return *m_containerType;
  if (m_parentCommonAttributes && m_parentCommonAttributes->m_containerType.has_value())
    return *m_parentCommonAttributes->m_containerType;

  return ContainerType::NOTYPE;
}

int PLAYLIST::CCommonAttribs::GetWidth() const
{
  if (m_resWidth > 0 || !m_parentCommonAttributes)
    return m_resWidth;
  return m_parentCommonAttributes->GetWidth();
}

int PLAYLIST::CCommonAttribs::GetHeight() const
{
  if (m_resHeight > 0 || !m_parentCommonAttributes)
    return m_resHeight;
  return m_parentCommonAttributes->GetHeight();
}

float PLAYLIST::CCommonAttribs::GetAspectRatio() const
{
  if (m_aspectRatio > 0 || !m_parentCommonAttributes)
    return m_aspectRatio;
  return m_parentCommonAttributes->GetAspectRatio();
}

uint32_t PLAYLIST::CCommonAttribs::GetFrameRate() const
{
  if (m_frameRate > 0 || !m_parentCommonAttributes)
    return m_frameRate;
  return m_parentCommonAttributes->GetFrameRate();
}

uint32_t PLAYLIST::CCommonAttribs::GetFrameRateScale() const
{
  if (m_frameRateScale > 0 || !m_parentCommonAttributes)
    return m_frameRateScale;
  return m_parentCommonAttributes->GetFrameRateScale();
}

uint32_t PLAYLIST::CCommonAttribs::GetSampleRate() const
{
  if (m_sampleRate > 0 || !m_parentCommonAttributes)
    return m_sampleRate;
  return m_parentCommonAttributes->GetSampleRate();
}

uint32_t PLAYLIST::CCommonAttribs::GetAudioChannels() const
{
  if (m_audioChannels > 0 || !m_parentCommonAttributes)
    return m_audioChannels;
  return m_parentCommonAttributes->GetAudioChannels();
}
