/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "HEVCCodecHandler.h"

HEVCCodecHandler::HEVCCodecHandler(AP4_SampleDescription* sd) : CodecHandler(sd)
{
  if (AP4_HevcSampleDescription* hevcSampleDescription =
          AP4_DYNAMIC_CAST(AP4_HevcSampleDescription, m_sampleDescription))
  {
    m_extraData.SetData(hevcSampleDescription->GetRawBytes().GetData(),
                        hevcSampleDescription->GetRawBytes().GetDataSize());
    m_naluLengthSize = hevcSampleDescription->GetNaluLengthSize();
  }
}

bool HEVCCodecHandler::ExtraDataToAnnexB()
{
  if (AP4_HevcSampleDescription* hevcSampleDescription =
          AP4_DYNAMIC_CAST(AP4_HevcSampleDescription, m_sampleDescription))
  {
    const AP4_Array<AP4_HvccAtom::Sequence>& sequences = hevcSampleDescription->GetSequences();

    if (sequences.ItemCount() == 0)
    {
      kodi::Log(ADDON_LOG_WARNING, "No available sequences for HEVC codec extra data");
      return false;
    }

    //calculate the size for annexb
    AP4_Size size{0};
    for (unsigned int i{0}; i < sequences.ItemCount(); ++i)
    {
      for (unsigned int j{0}; j < sequences[i].m_Nalus.ItemCount(); ++j)
      {
        size += sequences[i].m_Nalus[j].GetDataSize() + 4;
      }
    }

    m_extraData.SetDataSize(size);
    uint8_t* cursor(m_extraData.UseData());

    for (unsigned int i{0}; i < sequences.ItemCount(); ++i)
    {
      for (unsigned int j{0}; j < sequences[i].m_Nalus.ItemCount(); ++j)
      {
        cursor[0] = 0;
        cursor[1] = 0;
        cursor[2] = 0;
        cursor[3] = 1;
        memcpy(cursor + 4, sequences[i].m_Nalus[j].GetData(),
               sequences[i].m_Nalus[j].GetDataSize());
        cursor += sequences[i].m_Nalus[j].GetDataSize() + 4;
      }
    }
    kodi::Log(ADDON_LOG_DEBUG, "Converted %lu bytes HEVC codec extradata",
              m_extraData.GetDataSize());
    return true;
  }
  kodi::Log(ADDON_LOG_WARNING, "No HevcSampleDescription - annexb extradata not available");
  return false;
}

bool HEVCCodecHandler::GetInformation(kodi::addon::InputstreamInfo& info)
{
  if (info.GetFpsRate() == 0)
  {
    if (AP4_HevcSampleDescription* hevcSampleDescription =
            AP4_DYNAMIC_CAST(AP4_HevcSampleDescription, m_sampleDescription))
    {
      bool ret = false;
      if (hevcSampleDescription->GetAverageFrameRate() > 0)
      {
        info.SetFpsRate(hevcSampleDescription->GetAverageFrameRate());
        info.SetFpsScale(256);
        ret = true;
      }
      else if (hevcSampleDescription->GetConstantFrameRate() > 0)
      {
        info.SetFpsRate(hevcSampleDescription->GetConstantFrameRate());
        info.SetFpsScale(256);
        ret = true;
      }
      return ret;
    }
  }
  return false;
}
