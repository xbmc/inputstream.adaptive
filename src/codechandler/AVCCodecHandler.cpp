/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AVCCodecHandler.h"

#include "utils/Utils.h"

using namespace UTILS;

namespace
{
unsigned int ReadGolomb(AP4_BitReader& bits)
{
  unsigned int leading_zeros = 0;
  while (bits.ReadBit() == 0)
  {
    leading_zeros++;
    if (leading_zeros > 32)
      return 0; // safeguard
  }
  if (leading_zeros)
  {
    return (1 << leading_zeros) - 1 + bits.ReadBits(leading_zeros);
  }
  else
  {
    return 0;
  }
}
} // unnamed namespace

AVCCodecHandler::AVCCodecHandler(AP4_SampleDescription* sd)
  : CodecHandler{sd},
    m_countPictureSetIds{0},
    m_needSliceInfo{false},
    m_codecProfile{STREAMCODEC_PROFILE::CodecProfileUnknown}
{
  AP4_UI16 height{0};
  AP4_UI16 width{0};
  if (AP4_VideoSampleDescription* videoSampleDescription =
          AP4_DYNAMIC_CAST(AP4_VideoSampleDescription, m_sampleDescription))
  {
    width = videoSampleDescription->GetWidth();
    height = videoSampleDescription->GetHeight();
  }
  if (AP4_AvcSampleDescription* avcSampleDescription =
          AP4_DYNAMIC_CAST(AP4_AvcSampleDescription, m_sampleDescription))
  {
    m_extraData.SetData(avcSampleDescription->GetRawBytes().GetData(),
                        avcSampleDescription->GetRawBytes().GetDataSize());
    m_countPictureSetIds = avcSampleDescription->GetPictureParameters().ItemCount();
    m_naluLengthSize = avcSampleDescription->GetNaluLengthSize();
    m_needSliceInfo = (m_countPictureSetIds > 1 || width == 0 || height == 0);
    switch (avcSampleDescription->GetProfile())
    {
      case AP4_AVC_PROFILE_BASELINE:
        m_codecProfile = STREAMCODEC_PROFILE::H264CodecProfileBaseline;
        break;
      case AP4_AVC_PROFILE_MAIN:
        m_codecProfile = STREAMCODEC_PROFILE::H264CodecProfileMain;
        break;
      case AP4_AVC_PROFILE_EXTENDED:
        m_codecProfile = STREAMCODEC_PROFILE::H264CodecProfileExtended;
        break;
      case AP4_AVC_PROFILE_HIGH:
        m_codecProfile = STREAMCODEC_PROFILE::H264CodecProfileHigh;
        break;
      case AP4_AVC_PROFILE_HIGH_10:
        m_codecProfile = STREAMCODEC_PROFILE::H264CodecProfileHigh10;
        break;
      case AP4_AVC_PROFILE_HIGH_422:
        m_codecProfile = STREAMCODEC_PROFILE::H264CodecProfileHigh422;
        break;
      case AP4_AVC_PROFILE_HIGH_444:
        m_codecProfile = STREAMCODEC_PROFILE::H264CodecProfileHigh444Predictive;
        break;
    }
  }
}

bool AVCCodecHandler::ExtraDataToAnnexB()
{
  if (AP4_AvcSampleDescription* avcSampleDescription =
          AP4_DYNAMIC_CAST(AP4_AvcSampleDescription, m_sampleDescription))
  {
    //calculate the size for annexb
    AP4_Size sz(0);
    AP4_Array<AP4_DataBuffer>& pps(avcSampleDescription->GetPictureParameters());
    for (unsigned int i{0}; i < pps.ItemCount(); ++i)
      sz += 4 + pps[i].GetDataSize();
    AP4_Array<AP4_DataBuffer>& sps(avcSampleDescription->GetSequenceParameters());
    for (unsigned int i{0}; i < sps.ItemCount(); ++i)
      sz += 4 + sps[i].GetDataSize();

    m_extraData.SetDataSize(sz);
    AP4_Byte* cursor(m_extraData.UseData());

    for (unsigned int i{0}; i < sps.ItemCount(); ++i)
    {
      cursor[0] = 0;
      cursor[1] = 0;
      cursor[2] = 0;
      cursor[3] = 1;
      memcpy(cursor + 4, sps[i].GetData(), sps[i].GetDataSize());
      cursor += sps[i].GetDataSize() + 4;
    }
    for (unsigned int i{0}; i < pps.ItemCount(); ++i)
    {
      cursor[0] = 0;
      cursor[1] = 0;
      cursor[2] = 0;
      cursor[3] = 1;
      memcpy(cursor + 4, pps[i].GetData(), pps[i].GetDataSize());
      cursor += pps[i].GetDataSize() + 4;
    }
    return true;
  }
  return false;
}

void AVCCodecHandler::UpdatePPSId(const AP4_DataBuffer& buffer)
{
  if (!m_needSliceInfo)
    return;

  // Iterate data to find all NALU units slice headers of type 5 "Coded slice of an IDR picture"
  // to get the pic_parameter_set_id value of last NALU
  const AP4_Byte* data(buffer.GetData());
  AP4_Size dataSize(buffer.GetDataSize());
  for (; dataSize;)
  {
    // sanity check
    if (dataSize < m_naluLengthSize)
      break;

    // get the next NAL unit
    AP4_UI32 naluSize;
    switch (m_naluLengthSize)
    {
      case 1:
        naluSize = *data++;
        dataSize--;
        break;
      case 2:
        naluSize = AP4_BytesToUInt16BE(data);
        data += 2;
        dataSize -= 2;
        break;
      case 4:
        naluSize = AP4_BytesToUInt32BE(data);
        data += 4;
        dataSize -= 4;
        break;
      default:
        dataSize = 0;
        naluSize = 1;
        break;
    }
    if (naluSize > dataSize)
      break;

    // Stop further NALU processing
    if (m_countPictureSetIds < 2)
      m_needSliceInfo = false;

    unsigned int nalUnitType = *data & 0x1F;

    // Following code is a simplification of AP4_AvcFrameParser::ParseSliceHeader from AP4_AvcFrameParser::Feed
    if (nalUnitType == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_OF_IDR_PICTURE)
    {
      AP4_DataBuffer unescaped(data, dataSize);
      AP4_NalParser::Unescape(unescaped);
      AP4_BitReader bits(unescaped.GetData(), unescaped.GetDataSize());

      bits.SkipBits(8); // NAL Unit Type

      ReadGolomb(bits); // first_mb_in_slice
      ReadGolomb(bits); // slice_type
      m_pictureId = ReadGolomb(bits); // pic_parameter_set_id
    }
    // move to the next NAL unit
    data += naluSize;
    dataSize -= naluSize;
  }
}

bool AVCCodecHandler::GetInformation(kodi::addon::InputstreamInfo& info)
{
  if (m_pictureId == m_pictureIdPrev)
    return false;
  m_pictureIdPrev = m_pictureId;

  bool isChanged = UpdateInfoCodecName(info, CODEC::NAME_H264);

  uint32_t fourcc = 0;
  if (m_sampleDescription->GetFormat() == AP4_SAMPLE_FORMAT_AVC1)
    fourcc = CODEC::MakeFourCC(CODEC::FOURCC_AVC1);
  else if (m_sampleDescription->GetFormat() == AP4_SAMPLE_FORMAT_AVC2)
    fourcc = CODEC::MakeFourCC(CODEC::FOURCC_AVC2);
  else if (m_sampleDescription->GetFormat() == AP4_SAMPLE_FORMAT_AVC3)
    fourcc = CODEC::MakeFourCC(CODEC::FOURCC_AVC3);
  else if (m_sampleDescription->GetFormat() == AP4_SAMPLE_FORMAT_AVC4)
    fourcc = CODEC::MakeFourCC(CODEC::FOURCC_AVC4);

  if (fourcc != 0 && info.GetCodecFourCC() != fourcc)
  {
    info.SetCodecFourCC(fourcc);
    isChanged = true;
  }

  if (AP4_AvcSampleDescription* avcSampleDescription =
          AP4_DYNAMIC_CAST(AP4_AvcSampleDescription, m_sampleDescription))
  {
    AP4_Array<AP4_DataBuffer>& ppsList(avcSampleDescription->GetPictureParameters());
    AP4_AvcPictureParameterSet pps;
    for (unsigned int i(0); i < ppsList.ItemCount(); ++i)
    {
      AP4_AvcFrameParser fp;
      if (AP4_SUCCEEDED(fp.ParsePPS(ppsList[i].GetData(), ppsList[i].GetDataSize(), pps)) &&
          pps.pic_parameter_set_id == m_pictureId)
      {
        AP4_Array<AP4_DataBuffer>& spsList = avcSampleDescription->GetSequenceParameters();
        AP4_AvcSequenceParameterSet sps;
        for (unsigned int i{0}; i < spsList.ItemCount(); ++i)
        {
          if (AP4_SUCCEEDED(fp.ParseSPS(spsList[i].GetData(), spsList[i].GetDataSize(), sps)) &&
              sps.seq_parameter_set_id == pps.seq_parameter_set_id)
          {
            unsigned int width = info.GetWidth();
            unsigned int height = info.GetHeight();
            unsigned int fps_ticks = info.GetFpsRate();
            unsigned int fps_scale = info.GetFpsScale();
            float aspect = info.GetAspect();
            bool haveInfo = sps.GetInfo(width, height);
            haveInfo = sps.GetVUIInfo(fps_ticks, fps_scale, aspect) || haveInfo;
            if (haveInfo)
            {
              info.SetWidth(width);
              info.SetHeight(height);
              info.SetFpsRate(fps_ticks);
              info.SetFpsScale(fps_scale);
              info.SetAspect(aspect);
              isChanged = true;
            }
            break;
          }
        }
        break;
      }
    }
  }
  return isChanged;
};
