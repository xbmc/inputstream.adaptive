#include "CodecHandler.h"

bool CodecHandler::GetInformation(kodi::addon::InputstreamInfo& info)
{
  AP4_GenericAudioSampleDescription* asd(nullptr);
  if (sample_description)
  {
    if ((asd = dynamic_cast<AP4_GenericAudioSampleDescription*>(sample_description)))
    {
      if ((!info.GetChannels() && asd->GetChannelCount() != info.GetChannels()) ||
          (!info.GetSampleRate() && asd->GetSampleRate() != info.GetSampleRate()) ||
          (!info.GetBitsPerSample() && asd->GetSampleSize() != info.GetBitsPerSample()))
      {
        if (!info.GetChannels())
          info.SetChannels(asd->GetChannelCount());
        if (!info.GetSampleRate())
          info.SetSampleRate(asd->GetSampleRate());
        if (!info.GetBitsPerSample())
          info.SetBitsPerSample(asd->GetSampleSize());
        return true;
      }
    }
    else
    {
      //Netflix Framerate
      AP4_Atom* atom;
      AP4_UnknownUuidAtom* nxfr;
      static const AP4_UI08 uuid[16] = {0x4e, 0x65, 0x74, 0x66, 0x6c, 0x69, 0x78, 0x46,
                                        0x72, 0x61, 0x6d, 0x65, 0x52, 0x61, 0x74, 0x65};

      if ((atom =
               sample_description->GetDetails().GetChild(static_cast<const AP4_UI08*>(uuid), 0)) &&
          (nxfr = dynamic_cast<AP4_UnknownUuidAtom*>(atom)) && nxfr->GetData().GetDataSize() == 10)
      {
        AP4_UI16 fpsRate = nxfr->GetData().GetData()[7] | nxfr->GetData().GetData()[6] << 8;
        AP4_UI16 fpsScale = nxfr->GetData().GetData()[9] | nxfr->GetData().GetData()[8] << 8;

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