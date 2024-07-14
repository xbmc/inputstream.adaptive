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

void PLAYLIST::CRepresentation::SetParent(CAdaptationSet* parent /* = nullptr */,
                                          bool copyData /* = false */)
{
  CCommonSegAttribs::m_parentCommonSegAttribs = parent;

  // If you change parent, you will loose pointer where CCommonAttribs have data stored
  // so copy all data, and after replace CCommonAttribs
  // this is a workaround for a more complex problem see CDashTree::MergeAdpSets todo
  if (copyData && m_parentCommonAttributes)
  {
    if (m_parentCommonAttributes->GetContainerType() != ContainerType::NOTYPE &&
        m_containerType == ContainerType::NOTYPE)
      m_containerType = m_parentCommonAttributes->GetContainerType();

    if (m_parentCommonAttributes->GetAspectRatio() != 0 && m_aspectRatio == 0)
      m_aspectRatio = m_parentCommonAttributes->GetAspectRatio();

    if (m_parentCommonAttributes->GetFrameRate() != 0 && m_frameRate == 0)
      m_frameRate = m_parentCommonAttributes->GetFrameRate();

    if (m_parentCommonAttributes->GetFrameRateScale() != 0 && m_frameRateScale == 0)
      m_frameRateScale = m_parentCommonAttributes->GetFrameRateScale();

    if (m_parentCommonAttributes->GetWidth() != 0 && m_resWidth == 0)
      m_resWidth = m_parentCommonAttributes->GetWidth();

    if (m_parentCommonAttributes->GetHeight() != 0 && m_resHeight == 0)
      m_resHeight = m_parentCommonAttributes->GetHeight();

    if (m_parentCommonAttributes->GetSampleRate() != 0 && m_sampleRate == 0)
      m_sampleRate = m_parentCommonAttributes->GetSampleRate();

    if (m_parentCommonAttributes->GetAudioChannels() != 0 && m_audioChannels == 0)
      m_audioChannels = m_parentCommonAttributes->GetAudioChannels();

    if (!m_parentCommonAttributes->GetMimeType().empty() && m_mimeType.empty())
      m_mimeType = m_parentCommonAttributes->GetMimeType();
  }

  CCommonAttribs::m_parentCommonAttributes = parent;
}

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

const CSegment* PLAYLIST::CRepresentation::GetNextSegment()
{
  return m_segmentTimeline.GetNext(current_segment_);
}

const uint64_t PLAYLIST::CRepresentation::GetCurrentSegNumber() const
{
  return GetSegNumber(current_segment_);
}

const uint64_t PLAYLIST::CRepresentation::GetSegNumber(const CSegment* seg) const
{
  if (!seg)
    return SEGMENT_NO_NUMBER;

  const size_t segPos = m_segmentTimeline.GetPos(seg);

  if (segPos == SEGMENT_NO_POS)
    return SEGMENT_NO_NUMBER;

  return static_cast<uint64_t>(segPos) + m_startNumber;
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
