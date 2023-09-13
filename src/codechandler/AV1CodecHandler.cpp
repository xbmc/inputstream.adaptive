/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AV1CodecHandler.h"

#include "utils/Utils.h"

using namespace UTILS;

AV1CodecHandler::AV1CodecHandler(AP4_SampleDescription* sd)
  : CodecHandler(sd), m_codecProfile{STREAMCODEC_PROFILE::CodecProfileUnknown}
{
  if (AP4_Atom* atom = m_sampleDescription->GetDetails().GetChild(AP4_ATOM_TYPE_AV1C, 0))
  {
    AP4_Av1cAtom* av1c(AP4_DYNAMIC_CAST(AP4_Av1cAtom, atom));
    if (av1c)
    {
      switch (av1c->GetSeqProfile())
      {
        case AP4_AV1_PROFILE_MAIN:
          m_codecProfile = STREAMCODEC_PROFILE::AV1CodecProfileMain;
          break;
        case AP4_AV1_PROFILE_HIGH:
          m_codecProfile = STREAMCODEC_PROFILE::AV1CodecProfileHigh;
          break;
        case AP4_AV1_PROFILE_PROFESSIONAL:
          m_codecProfile = STREAMCODEC_PROFILE::AV1CodecProfileProfessional;
          break;
        default:
          m_codecProfile = STREAMCODEC_PROFILE::AV1CodecProfileMain;
      }

      const AP4_DataBuffer& obus{av1c->GetConfigObus()};
      m_extraData.SetData(obus.GetData(), obus.GetDataSize());
    }
  }
}

bool AV1CodecHandler::GetInformation(kodi::addon::InputstreamInfo& info)
{
  bool isChanged = CodecHandler::GetInformation(info);

  isChanged |= UpdateInfoCodecName(info, CODEC::NAME_AV1);

  if (info.GetCodecProfile() != m_codecProfile)
  {
    info.SetCodecProfile(m_codecProfile);
    isChanged = true;
  }

  return isChanged;
}
