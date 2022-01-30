#include "AVCCodecHandler.h"

AVCCodecHandler::AVCCodecHandler(AP4_SampleDescription* sd)
  : CodecHandler(sd), countPictureSetIds(0), needSliceInfo(false)
{
  unsigned int width(0), height(0);
  if (AP4_VideoSampleDescription* video_sample_description =
          AP4_DYNAMIC_CAST(AP4_VideoSampleDescription, sample_description))
  {
    width = video_sample_description->GetWidth();
    height = video_sample_description->GetHeight();
  }
  if (AP4_AvcSampleDescription* avc =
          AP4_DYNAMIC_CAST(AP4_AvcSampleDescription, sample_description))
  {
    extra_data.SetData(avc->GetRawBytes().GetData(), avc->GetRawBytes().GetDataSize());
    countPictureSetIds = avc->GetPictureParameters().ItemCount();
    naluLengthSize = avc->GetNaluLengthSize();
    needSliceInfo = (countPictureSetIds > 1 || !width || !height);
    switch (avc->GetProfile())
    {
      case AP4_AVC_PROFILE_BASELINE:
        codecProfile = STREAMCODEC_PROFILE::H264CodecProfileBaseline;
        break;
      case AP4_AVC_PROFILE_MAIN:
        codecProfile = STREAMCODEC_PROFILE::H264CodecProfileMain;
        break;
      case AP4_AVC_PROFILE_EXTENDED:
        codecProfile = STREAMCODEC_PROFILE::H264CodecProfileExtended;
        break;
      case AP4_AVC_PROFILE_HIGH:
        codecProfile = STREAMCODEC_PROFILE::H264CodecProfileHigh;
        break;
      case AP4_AVC_PROFILE_HIGH_10:
        codecProfile = STREAMCODEC_PROFILE::H264CodecProfileHigh10;
        break;
      case AP4_AVC_PROFILE_HIGH_422:
        codecProfile = STREAMCODEC_PROFILE::H264CodecProfileHigh422;
        break;
      case AP4_AVC_PROFILE_HIGH_444:
        codecProfile = STREAMCODEC_PROFILE::H264CodecProfileHigh444Predictive;
        break;
      default:
        codecProfile = STREAMCODEC_PROFILE::CodecProfileUnknown;
        break;
    }
  }
}

bool AVCCodecHandler::ExtraDataToAnnexB()
{
  if (AP4_AvcSampleDescription* avc =
          AP4_DYNAMIC_CAST(AP4_AvcSampleDescription, sample_description))
  {
    //calculate the size for annexb
    size_t sz(0);
    AP4_Array<AP4_DataBuffer>& pps(avc->GetPictureParameters());
    for (unsigned int i(0); i < pps.ItemCount(); ++i)
      sz += 4 + pps[i].GetDataSize();
    AP4_Array<AP4_DataBuffer>& sps(avc->GetSequenceParameters());
    for (unsigned int i(0); i < sps.ItemCount(); ++i)
      sz += 4 + sps[i].GetDataSize();

    extra_data.SetDataSize(sz);
    uint8_t* cursor(extra_data.UseData());

    for (unsigned int i(0); i < sps.ItemCount(); ++i)
    {
      cursor[0] = cursor[1] = cursor[2] = 0;
      cursor[3] = 1;
      memcpy(cursor + 4, sps[i].GetData(), sps[i].GetDataSize());
      cursor += sps[i].GetDataSize() + 4;
    }
    for (unsigned int i(0); i < pps.ItemCount(); ++i)
    {
      cursor[0] = cursor[1] = cursor[2] = 0;
      cursor[3] = 1;
      memcpy(cursor + 4, pps[i].GetData(), pps[i].GetDataSize());
      cursor += pps[i].GetDataSize() + 4;
    }
    return true;
  }
  return false;
}

void AVCCodecHandler::UpdatePPSId(AP4_DataBuffer const& buffer)
{
  if (!needSliceInfo)
    return;

  //Search the Slice header NALU
  const AP4_UI08* data(buffer.GetData());
  unsigned int data_size(buffer.GetDataSize());
  for (; data_size;)
  {
    // sanity check
    if (data_size < naluLengthSize)
      break;

    // get the next NAL unit
    AP4_UI32 nalu_size;
    switch (naluLengthSize)
    {
      case 1:
        nalu_size = *data++;
        data_size--;
        break;
      case 2:
        nalu_size = AP4_BytesToInt16BE(data);
        data += 2;
        data_size -= 2;
        break;
      case 4:
        nalu_size = AP4_BytesToInt32BE(data);
        data += 4;
        data_size -= 4;
        break;
      default:
        data_size = 0;
        nalu_size = 1;
        break;
    }
    if (nalu_size > data_size)
      break;

    // Stop further NALU processing
    if (countPictureSetIds < 2)
      needSliceInfo = false;

    unsigned int nal_unit_type = *data & 0x1F;

    if (
        //nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_OF_NON_IDR_PICTURE ||
        nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_OF_IDR_PICTURE //||
        //nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_A ||
        //nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_B ||
        //nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_C
    )
    {

      AP4_DataBuffer unescaped(data, data_size);
      AP4_NalParser::Unescape(unescaped);
      AP4_BitReader bits(unescaped.GetData(), unescaped.GetDataSize());

      bits.SkipBits(8); // NAL Unit Type

      AP4_AvcFrameParser::ReadGolomb(bits); // first_mb_in_slice
      AP4_AvcFrameParser::ReadGolomb(bits); // slice_type
      pictureId = AP4_AvcFrameParser::ReadGolomb(bits); //picture_set_id
    }
    // move to the next NAL unit
    data += nalu_size;
    data_size -= nalu_size;
  }
}

bool AVCCodecHandler::GetInformation(kodi::addon::InputstreamInfo& info)
{
  if (pictureId == pictureIdPrev)
    return false;
  pictureIdPrev = pictureId;

  if (AP4_AvcSampleDescription* avc =
          AP4_DYNAMIC_CAST(AP4_AvcSampleDescription, sample_description))
  {
    AP4_Array<AP4_DataBuffer>& ppsList(avc->GetPictureParameters());
    AP4_AvcPictureParameterSet pps;
    for (unsigned int i(0); i < ppsList.ItemCount(); ++i)
    {
      AP4_AvcFrameParser fp;
      if (AP4_SUCCEEDED(fp.ParsePPS(ppsList[i].GetData(), ppsList[i].GetDataSize(), pps)) &&
          pps.pic_parameter_set_id == pictureId)
      {
        AP4_Array<AP4_DataBuffer>& spsList = avc->GetSequenceParameters();
        AP4_AvcSequenceParameterSet sps;
        for (unsigned int i(0); i < spsList.ItemCount(); ++i)
        {
          if (AP4_SUCCEEDED(fp.ParseSPS(spsList[i].GetData(), spsList[i].GetDataSize(), sps)) &&
              sps.seq_parameter_set_id == pps.seq_parameter_set_id)
          {
            unsigned int width = info.GetWidth();
            unsigned int height = info.GetHeight();
            unsigned int fps_ticks = info.GetFpsRate();
            unsigned int fps_scale = info.GetFpsScale();
            float aspect = info.GetAspect();
            bool ret = sps.GetInfo(width, height);
            ret = sps.GetVUIInfo(fps_ticks, fps_scale, aspect) || ret;
            if (ret)
            {
              info.SetWidth(width);
              info.SetHeight(height);
              info.SetFpsRate(fps_ticks);
              info.SetFpsScale(fps_scale);
              info.SetAspect(aspect);
            }
            return ret;
          }
        }
        break;
      }
    }
  }
  return false;
};
