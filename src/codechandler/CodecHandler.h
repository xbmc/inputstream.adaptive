#pragma once
#include <bento4/Ap4.h>
#include <kodi/AddonBase.h>
#include <kodi/addon-instance/Inputstream.h>

class ATTR_DLL_LOCAL CodecHandler
{
public:
  CodecHandler(AP4_SampleDescription* sd)
    : sample_description(sd), naluLengthSize(0), pictureId(0), pictureIdPrev(0xFF){};
  virtual ~CodecHandler(){};

  virtual void UpdatePPSId(AP4_DataBuffer const&){};
  virtual bool GetInformation(kodi::addon::InputstreamInfo& info);
  virtual bool ExtraDataToAnnexB() { return false; };
  virtual STREAMCODEC_PROFILE GetProfile() { return STREAMCODEC_PROFILE::CodecProfileNotNeeded; };
  virtual bool Transform(AP4_UI64 pts, AP4_UI32 duration, AP4_DataBuffer& buf, AP4_UI64 timescale)
  {
    return false;
  };
  virtual bool ReadNextSample(AP4_Sample& sample, AP4_DataBuffer& buf) { return false; };
  virtual void SetPTSOffset(AP4_UI64 offset){};
  virtual bool TimeSeek(AP4_UI64 seekPos) { return true; };
  virtual void Reset(){};

  AP4_SampleDescription* sample_description;
  AP4_DataBuffer extra_data;
  AP4_UI08 naluLengthSize;
  AP4_UI08 pictureId, pictureIdPrev;
};