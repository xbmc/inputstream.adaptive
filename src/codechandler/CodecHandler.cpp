/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CodecHandler.h"

namespace
{
constexpr const char* NETFLIX_FRAMERATE_UUID = "NetflixFrameRate";
}

bool CodecHandler::GetInformation(kodi::addon::InputstreamInfo& info)
{
  if (m_sampleDescription->GetType() != AP4_SampleDescription::Type::TYPE_SUBTITLES &&
      m_sampleDescription->GetType() != AP4_SampleDescription::Type::TYPE_UNKNOWN)
  {
    // Netflix Framerate
    AP4_Atom* atom;
    AP4_UnknownUuidAtom* nxfr;
    atom = m_sampleDescription->GetDetails().GetChild(
        reinterpret_cast<const AP4_UI08*>(NETFLIX_FRAMERATE_UUID));
    if (atom && (nxfr = dynamic_cast<AP4_UnknownUuidAtom*>(atom)) &&
        nxfr->GetData().GetDataSize() == 10)
    {
      unsigned int fpsRate = nxfr->GetData().GetData()[7] | nxfr->GetData().GetData()[6] << 8;
      unsigned int fpsScale = nxfr->GetData().GetData()[9] | nxfr->GetData().GetData()[8] << 8;

      if (info.GetFpsScale() != fpsScale || info.GetFpsRate() != fpsRate)
      {
        info.SetFpsScale(fpsScale);
        info.SetFpsRate(fpsRate);
        return true;
      }
    }
  }
  return false;
}

bool CodecHandler::UpdateInfoCodecName(kodi::addon::InputstreamInfo& info, const char* codecName)
{
  bool isChanged{false};

  if (info.GetCodecName() != codecName)
  {
    info.SetCodecName(codecName);
    isChanged = true;
  }

  AP4_String codecStr;
  m_sampleDescription->GetCodecString(codecStr);
  if (isChanged && codecStr.GetLength() > 0 && info.GetCodecInternalName() != codecStr.GetChars())
  {
    info.SetCodecInternalName(codecStr.GetChars());
    isChanged = true;
  }

  return isChanged;
};
