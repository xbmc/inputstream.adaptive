/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Representation.h"

#include "utils/StringUtils.h"

#include <kodi/addon-instance/inputstream/TimingConstants.h>

using namespace PLAYLIST;
using namespace UTILS;

void PLAYLIST::CRepresentation::AddCodecs(std::string_view codecs)
{
  std::set<std::string> list = STRING::SplitToSet(codecs, ',');
  m_codecs.insert(list.begin(), list.end());
}

void PLAYLIST::CRepresentation::AddCodecs(const std::set<std::string>& codecs)
{
  m_codecs.insert(codecs.begin(), codecs.end());
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
  m_isEnabled = other->m_isEnabled;
}

void PLAYLIST::CRepresentation::SetScaling()
{
  if (!m_timescale)
  {
    timescale_ext_ = timescale_int_ = 1;
    return;
  }

  timescale_ext_ = STREAM_TIME_BASE;
  timescale_int_ = m_timescale;

  while (timescale_ext_ > 1)
  {
    if ((timescale_int_ / 10) * 10 == timescale_int_)
    {
      timescale_ext_ /= 10;
      timescale_int_ /= 10;
    }
    else
      break;
  }
}
