#include "CodecHandler.h"

class ATTR_DLL_LOCAL HEVCCodecHandler : public CodecHandler
{
public:
  HEVCCodecHandler(AP4_SampleDescription* sd);

  bool ExtraDataToAnnexB() override;
  bool GetInformation(kodi::addon::InputstreamInfo& info) override;
};
