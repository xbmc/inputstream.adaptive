/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "VP9CodecHandler.h"

#include "utils/Utils.h"

using namespace UTILS;

VP9CodecHandler::VP9CodecHandler(AP4_SampleDescription* sd) : CodecHandler(sd)
{
  if (AP4_Atom* atom = m_sampleDescription->GetDetails().GetChild(AP4_ATOM_TYPE_VPCC, 0))
  {
    AP4_VpccAtom* vpcc(AP4_DYNAMIC_CAST(AP4_VpccAtom, atom));
    if (vpcc)
      m_extraData.SetData(vpcc->GetData().GetData(), vpcc->GetData().GetDataSize());
  }
}

bool VP9CodecHandler::GetInformation(kodi::addon::InputstreamInfo& info)
{
  bool isChanged = CodecHandler::GetInformation(info);

  isChanged |= UpdateInfoCodecName(info, CODEC::NAME_VP9);

  return isChanged;
}
