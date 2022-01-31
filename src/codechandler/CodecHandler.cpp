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
  AP4_GenericAudioSampleDescription* audioSampleDescription(nullptr);
  if (m_sampleDescription)
  {
    if ((audioSampleDescription =
             dynamic_cast<AP4_GenericAudioSampleDescription*>(m_sampleDescription)))
    {
      if ((info.GetChannels() == 0 &&
           audioSampleDescription->GetChannelCount() != info.GetChannels()) ||
          (info.GetSampleRate() == 0 &&
           audioSampleDescription->GetSampleRate() != info.GetSampleRate()) ||
          (info.GetBitsPerSample() == 0 &&
           audioSampleDescription->GetSampleSize() != info.GetBitsPerSample()))
      {
        if (info.GetChannels() == 0)
          info.SetChannels(audioSampleDescription->GetChannelCount());
        if (info.GetSampleRate() == 0)
          info.SetSampleRate(audioSampleDescription->GetSampleRate());
        if (info.GetBitsPerSample() == 0)
          info.SetBitsPerSample(audioSampleDescription->GetSampleSize());
        return true;
      }
    }
    else
    {
      //Netflix Framerate
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
  }
  return false;
};
