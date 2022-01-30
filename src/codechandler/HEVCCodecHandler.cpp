#include "HEVCCodecHandler.h"

HEVCCodecHandler::HEVCCodecHandler(AP4_SampleDescription* sd) : CodecHandler(sd)
{
  if (AP4_HevcSampleDescription* hevc =
          AP4_DYNAMIC_CAST(AP4_HevcSampleDescription, sample_description))
  {
    extra_data.SetData(hevc->GetRawBytes().GetData(), hevc->GetRawBytes().GetDataSize());
    naluLengthSize = hevc->GetNaluLengthSize();
  }
}

bool HEVCCodecHandler::ExtraDataToAnnexB()
{
  if (AP4_HevcSampleDescription* hevc =
          AP4_DYNAMIC_CAST(AP4_HevcSampleDescription, sample_description))
  {
    const AP4_Array<AP4_HvccAtom::Sequence>& sequences = hevc->GetSequences();

    if (!sequences.ItemCount())
    {
      kodi::Log(ADDON_LOG_WARNING, "No available sequences for HEVC codec extra data");
      return false;
    }

    //calculate the size for annexb
    size_t sz(0);
    for (const AP4_HvccAtom::Sequence *b(&sequences[0]), *e(&sequences[sequences.ItemCount()]);
         b != e; ++b)
      for (const AP4_DataBuffer *bn(&b->m_Nalus[0]), *en(&b->m_Nalus[b->m_Nalus.ItemCount()]);
           bn != en; ++bn)
        sz += (4 + bn->GetDataSize());

    extra_data.SetDataSize(sz);
    uint8_t* cursor(extra_data.UseData());

    for (const AP4_HvccAtom::Sequence *b(&sequences[0]), *e(&sequences[sequences.ItemCount()]);
         b != e; ++b)
      for (const AP4_DataBuffer *bn(&b->m_Nalus[0]), *en(&b->m_Nalus[b->m_Nalus.ItemCount()]);
           bn != en; ++bn)
      {
        cursor[0] = cursor[1] = cursor[2] = 0;
        cursor[3] = 1;
        memcpy(cursor + 4, bn->GetData(), bn->GetDataSize());
        cursor += bn->GetDataSize() + 4;
      }
    kodi::Log(ADDON_LOG_DEBUG, "Converted %lu bytes HEVC codec extradata",
              extra_data.GetDataSize());
    return true;
  }
  kodi::Log(ADDON_LOG_WARNING, "No HevcSampleDescription - annexb extradata not available");
  return false;
}

bool HEVCCodecHandler::GetInformation(kodi::addon::InputstreamInfo& info)
{
  if (!info.GetFpsRate())
  {
    if (AP4_HevcSampleDescription* hevc =
            AP4_DYNAMIC_CAST(AP4_HevcSampleDescription, sample_description))
    {
      bool ret = false;
      if (hevc->GetAverageFrameRate())
      {
        info.SetFpsRate(hevc->GetAverageFrameRate());
        info.SetFpsScale(256);
        ret = true;
      }
      else if (hevc->GetConstantFrameRate())
      {
        info.SetFpsRate(hevc->GetConstantFrameRate());
        info.SetFpsScale(256);
        ret = true;
      }
      return ret;
    }
  }
  return false;
}
