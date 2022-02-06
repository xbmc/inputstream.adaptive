/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WebVTTCodecHandler.h"

namespace
{
constexpr AP4_Byte EXTRADATA_FILE[4] = {'f', 'i', 'l', 'e'};
constexpr AP4_Byte EXTRADATA_FMP4[4] = {'f', 'm', 'p', '4'};
} // namespace

WebVTTCodecHandler::WebVTTCodecHandler(AP4_SampleDescription* sd, bool asFile)
  : CodecHandler(sd), m_ptsOffset(0)
{
  // Note: the extra data must be exactly of 4 characters as imposed in kodi core
  if (asFile)
  {
    // Inform Kodi subtitle parser that we process the data as single file
    m_extraData.SetData(reinterpret_cast<const AP4_Byte*>(&EXTRADATA_FILE), 4);
  }
  else if (sd) // WebVTT ISOBMFF format type (ISO/IEC 14496-30:2014)
  {
    // Inform Kodi subtitle parser that we process data as ISOBMFF format type
    m_extraData.SetData(reinterpret_cast<const AP4_Byte*>(&EXTRADATA_FMP4), 4);
  }
};

bool WebVTTCodecHandler::Transform(AP4_UI64 pts,
                                   AP4_UI32 duration,
                                   AP4_DataBuffer& buf,
                                   AP4_UI64 timescale)
{
  m_data.SetData(buf.GetData(), buf.GetDataSize());
  m_pts = pts;
  m_duration = duration;
  return true;
}

bool WebVTTCodecHandler::ReadNextSample(AP4_Sample& sample, AP4_DataBuffer& buf)
{
  if (m_data.GetDataSize() > 0)
  {
    buf.SetData(m_data.GetData(), m_data.GetDataSize());
    sample.SetDts(m_pts);
    sample.SetCtsDelta(0);
    sample.SetDuration(m_duration);
    // Reset the source data size otherwise we send always same data without end
    m_data.SetDataSize(0);
    return true;
  }
  buf.SetDataSize(0);
  return false;
}
