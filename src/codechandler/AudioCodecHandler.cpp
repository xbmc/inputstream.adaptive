/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AudioCodecHandler.h"

#include "../utils/Utils.h"

using namespace UTILS;

AudioCodecHandler::AudioCodecHandler(AP4_SampleDescription* sd) : CodecHandler(sd)
{
  if (m_sampleDescription->GetFormat() == AP4_SAMPLE_FORMAT_MP4A)
  {
    // Get extradata for types like aac
    AP4_MpegSampleDescription* mpegSd =
        AP4_DYNAMIC_CAST(AP4_MpegSampleDescription, m_sampleDescription);
    m_extraData.SetData(mpegSd->GetDecoderInfo().GetData(), mpegSd->GetDecoderInfo().GetDataSize());
  }
}

bool AudioCodecHandler::GetInformation(kodi::addon::InputstreamInfo& info)
{
  bool isChanged{false};

  if (m_sampleDescription->GetType() == AP4_SampleDescription::TYPE_MPEG)
  {
    std::string codecName;

    switch (static_cast<AP4_MpegSampleDescription*>(m_sampleDescription)->GetObjectTypeId())
    {
      case AP4_OTI_MPEG4_AUDIO:
      case AP4_OTI_MPEG2_AAC_AUDIO_MAIN:
      case AP4_OTI_MPEG2_AAC_AUDIO_LC:
      case AP4_OTI_MPEG2_AAC_AUDIO_SSRP:
        codecName = CODEC::NAME_AAC;
        break;
      case AP4_OTI_DTS_AUDIO:
      case AP4_OTI_DTS_HIRES_AUDIO:
      case AP4_OTI_DTS_MASTER_AUDIO:
      case AP4_OTI_DTS_EXPRESS_AUDIO:
        codecName = CODEC::NAME_DTS;
        break;
      case AP4_OTI_AC3_AUDIO:
        codecName = CODEC::NAME_AC3;
        break;
      case AP4_OTI_EAC3_AUDIO:
        codecName = CODEC::NAME_EAC3;
        break;
    }
    if (!codecName.empty())
      isChanged = UpdateInfoCodecName(info, codecName.c_str());
  }

  if (AP4_AudioSampleDescription* audioSd =
          AP4_DYNAMIC_CAST(AP4_AudioSampleDescription, m_sampleDescription))
  {
    // Note: channel count field on audio sample description atom v0 and v1
    // have the max value of 2 channels, we dont have to override existing higher values
    if (audioSd->GetChannelCount() > 0 && audioSd->GetChannelCount() > info.GetChannels())
    {
      info.SetChannels(audioSd->GetChannelCount());
      isChanged = true;
    }
    if (audioSd->GetSampleRate() > 0 && audioSd->GetSampleRate() != info.GetSampleRate())
    {
      info.SetSampleRate(audioSd->GetSampleRate());
      isChanged = true;
    }
    if (audioSd->GetSampleSize() > 0 && audioSd->GetSampleSize() != info.GetBitsPerSample())
    {
      info.SetBitsPerSample(audioSd->GetSampleSize());
      isChanged = true;
    }
  }

  return isChanged;
}
