/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Representation.h"

#include "../utils/StringUtils.h"

#include <algorithm> // any_of, find_if

using namespace PLAYLIST;
using namespace UTILS;

void PLAYLIST::CRepresentation::AddCodecs(std::string_view codecs)
{
  std::set<std::string> list = STRING::Split(codecs.data(), ',');
  m_codecs.insert(list.begin(), list.end());
}

void PLAYLIST::CRepresentation::AddCodecs(const std::set<std::string>& codecs)
{
  m_codecs.insert(codecs.begin(), codecs.end());
}

bool PLAYLIST::CRepresentation::ContainsCodec(std::string_view codec) const
{
  if (std::any_of(m_codecs.begin(), m_codecs.end(),
                  [codec](const std::string_view name) { return STRING::Contains(name, codec); }))
  {
    return true;
  }
  return false;
}

bool PLAYLIST::CRepresentation::ContainsCodec(std::string_view codec, std::string& codecStr) const
{
  auto itCodec =
      std::find_if(m_codecs.begin(), m_codecs.end(),
                   [codec](const std::string_view name) { return STRING::Contains(name, codec); });
  if (itCodec != m_codecs.end())
  {
    codecStr = *itCodec;
    return true;
  }
  codecStr.clear();
  return false;
}

void PLAYLIST::CRepresentation::CopyHLSData(const CRepresentation* other)
{
  m_id = other->m_id;
  m_codecs = other->m_codecs;
  m_codecPrivateData = other->m_codecPrivateData;
  m_baseUrl = other->m_baseUrl;
  m_sourceUrl = other->m_sourceUrl;
  m_bandwidth = other->m_bandwidth;
  m_sampleRate = other->m_sampleRate;
  m_resWidth = other->m_resWidth;
  m_resHeight = other->m_resHeight;
  m_frameRate = other->m_frameRate;
  m_frameRateScale = other->m_frameRateScale;
  m_aspectRatio = other->m_aspectRatio;
  m_hdcpVersion = other->m_hdcpVersion;
  m_audioChannels = other->m_audioChannels;
  m_containerType = other->m_containerType;
  m_timescale = other->m_timescale;
  timescale_ext_ = other->timescale_ext_;
  timescale_int_ = other->timescale_int_;

  m_isIncludedStream = other->m_isIncludedStream;
  m_hasSegmentsUrl = other->m_hasSegmentsUrl;
  m_isEnabled = other->m_isEnabled;
  m_isWaitForSegment = other->m_isWaitForSegment;
  m_isDownloaded = other->m_isDownloaded;
  m_initSegment = other->m_initSegment;
}
