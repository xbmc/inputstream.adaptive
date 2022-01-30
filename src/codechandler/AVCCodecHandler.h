#include "CodecHandler.h"

class ATTR_DLL_LOCAL AVCCodecHandler : public CodecHandler
{
public:
  AVCCodecHandler(AP4_SampleDescription* sd);
  bool ExtraDataToAnnexB() override;
  void UpdatePPSId(AP4_DataBuffer const& buffer) override;
  bool GetInformation(kodi::addon::InputstreamInfo& info) override;
  STREAMCODEC_PROFILE GetProfile() override { return codecProfile; };

private:
  unsigned int countPictureSetIds;
  STREAMCODEC_PROFILE codecProfile;
  bool needSliceInfo;
};
