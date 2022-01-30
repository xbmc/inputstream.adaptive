#include "MPEGCodecHandler.h"

MPEGCodecHandler::MPEGCodecHandler(AP4_SampleDescription* sd) : CodecHandler(sd)
{
  if (AP4_MpegSampleDescription* aac =
          AP4_DYNAMIC_CAST(AP4_MpegSampleDescription, sample_description))
    extra_data.SetData(aac->GetDecoderInfo().GetData(), aac->GetDecoderInfo().GetDataSize());
}
