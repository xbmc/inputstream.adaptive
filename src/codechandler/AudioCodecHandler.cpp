/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AudioCodecHandler.h"

#include "../utils/Utils.h"

#include <bento4/Ap4Mp4AudioInfo.h>

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
  if (!m_sampleDescription)
    return false;

  bool isChanged{false};
  std::string codecName;
  STREAMCODEC_PROFILE codecProfile{CodecProfileUnknown};

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

  if (m_sampleDescription->GetType() == AP4_SampleDescription::TYPE_MPEG)
  {
    switch (static_cast<AP4_MpegSampleDescription*>(m_sampleDescription)->GetObjectTypeId())
    {
      case AP4_OTI_MPEG4_AUDIO:
        codecName = CODEC::NAME_AAC;
        codecProfile = static_cast<STREAMCODEC_PROFILE>(GetMpeg4AACProfile());
        break;
      case AP4_OTI_MPEG2_AAC_AUDIO_MAIN:
        codecName = CODEC::NAME_AAC;
        codecProfile = AACCodecProfileMAIN;
        break;
      case AP4_OTI_MPEG2_AAC_AUDIO_LC:
        codecName = CODEC::NAME_AAC;
        codecProfile = MPEG2AACCodecProfileLOW;
        break;
      case AP4_OTI_MPEG2_AAC_AUDIO_SSRP:
        codecName = CODEC::NAME_AAC;
        break;
      case AP4_OTI_DTS_AUDIO:
        codecName = CODEC::NAME_DTS;
        codecProfile = DTSCodecProfile;
        break;
      case AP4_OTI_DTS_HIRES_AUDIO:
        codecName = CODEC::NAME_DTS;
        codecProfile = DTSCodecProfileHDHRA;
        break;
      case AP4_OTI_DTS_MASTER_AUDIO:
        codecName = CODEC::NAME_DTS;
        codecProfile = DTSCodecProfileHDMA;
        break;
      case AP4_OTI_DTS_EXPRESS_AUDIO:
        codecName = CODEC::NAME_DTS;
        codecProfile = DTSCodecProfileHDExpress;
        break;
      case AP4_OTI_AC3_AUDIO:
        codecName = CODEC::NAME_AC3;
        break;
      case AP4_OTI_EAC3_AUDIO:
      {
        codecName = CODEC::NAME_EAC3;
        AP4_Dec3Atom* dec3 = AP4_DYNAMIC_CAST(
            AP4_Dec3Atom, m_sampleDescription->GetDetails().GetChild(AP4_ATOM_TYPE_DEC3));
        if (dec3 && dec3->GetFlagEC3ExtensionTypeA() > 0)
        {
          codecProfile = DDPlusCodecProfileAtmos;
          // The channels value should match the value of the complexity_index_type_a field
          if (dec3->GetComplexityIndexTypeA() > 0 &&
              dec3->GetComplexityIndexTypeA() != info.GetChannels())
          {
            info.SetChannels(dec3->GetComplexityIndexTypeA());
            isChanged = true;
          }
        }
        break;
      }
    }
  }
  else if (m_sampleDescription->GetType() == AP4_SampleDescription::TYPE_EAC3)
  {
    codecName = CODEC::NAME_EAC3;
    AP4_Dec3Atom* dec3 = AP4_DYNAMIC_CAST(
        AP4_Dec3Atom, m_sampleDescription->GetDetails().GetChild(AP4_ATOM_TYPE_DEC3));
    if (dec3 && dec3->GetFlagEC3ExtensionTypeA() > 0)
    {
      codecProfile = DDPlusCodecProfileAtmos;
      // The channels value should match the value of the complexity_index_type_a field
      if (dec3->GetComplexityIndexTypeA() > 0 &&
          dec3->GetComplexityIndexTypeA() != info.GetChannels())
      {
        info.SetChannels(dec3->GetComplexityIndexTypeA());
        isChanged = true;
      }
    }
  }
  else if (m_sampleDescription->GetType() == AP4_SampleDescription::TYPE_AC3)
  {
    codecName = CODEC::NAME_AC3;
  }

  if (!codecName.empty())
    isChanged = UpdateInfoCodecName(info, codecName.c_str());

  if (codecProfile != CodecProfileUnknown && info.GetCodecProfile() != codecProfile)
  {
    info.SetCodecProfile(codecProfile);
    isChanged = true;
  }

  return isChanged;
}

int AudioCodecHandler::GetMpeg4AACProfile()
{
  AP4_MpegAudioSampleDescription* mpegDesc =
      AP4_DYNAMIC_CAST(AP4_MpegAudioSampleDescription, m_sampleDescription);
  if (mpegDesc)
  {
    AP4_MpegAudioSampleDescription::Mpeg4AudioObjectType ot = mpegDesc->GetMpeg4AudioObjectType();
    switch (ot)
    {
      case AP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_MAIN:
        return AACCodecProfileMAIN;
        break;
      case AP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_LC:
      {
        const AP4_DataBuffer& dsi = mpegDesc->GetDecoderInfo();
        if (dsi.GetDataSize() > 0)
        {
          AP4_Mp4AudioDecoderConfig decConfig;
          AP4_Result result = decConfig.Parse(dsi.GetData(), dsi.GetDataSize());
          if (AP4_SUCCEEDED(result))
          {
            if (decConfig.m_Extension.m_PsPresent)
            {
              return AACCodecProfileHEV2; // HE-AAC v2
            }
            else if (decConfig.m_Extension.m_SbrPresent)
            {
              return AACCodecProfileHE; // HE-AAC
            }
          }
        }
        return AACCodecProfileLOW;
        break;
      }
      case AP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_SSR:
        return AACCodecProfileSSR;
        break;
      case AP4_MPEG4_AUDIO_OBJECT_TYPE_AAC_LTP:
        return AACCodecProfileLTP;
        break;
      case AP4_MPEG4_AUDIO_OBJECT_TYPE_SBR:
        return AACCodecProfileHE; // HE-AAC
        break;
      case AP4_MPEG4_AUDIO_OBJECT_TYPE_PS:
        return AACCodecProfileHEV2; // HE-AAC v2
        break;
      default:
        break;
    }
  }
  return CodecProfileUnknown;
}
