#include "VP9CodecHandler.h"

VP9CodecHandler::VP9CodecHandler(AP4_SampleDescription* sd) : CodecHandler(sd)
{
  if (AP4_Atom* atom = sample_description->GetDetails().GetChild(AP4_ATOM_TYPE_VPCC, 0))
  {
    AP4_VpccAtom* vpcc(AP4_DYNAMIC_CAST(AP4_VpccAtom, atom));
    if (vpcc)
      extra_data.SetData(vpcc->GetData().GetData(), vpcc->GetData().GetDataSize());
  }
}
