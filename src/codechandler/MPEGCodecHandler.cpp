/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MPEGCodecHandler.h"

MPEGCodecHandler::MPEGCodecHandler(AP4_SampleDescription* sd) : CodecHandler(sd)
{
  if (AP4_MpegSampleDescription* aac =
          AP4_DYNAMIC_CAST(AP4_MpegSampleDescription, m_sampleDescription))
    m_extraData.SetData(aac->GetDecoderInfo().GetData(), aac->GetDecoderInfo().GetDataSize());
}
