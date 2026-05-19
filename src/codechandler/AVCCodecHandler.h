/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "CodecHandler.h"

class ATTR_DLL_LOCAL AVCCodecHandler : public CodecHandler
{
public:
  AVCCodecHandler(AP4_SampleDescription* sd);
  bool ExtraDataToAnnexB() override;
  void UpdatePPSId(const AP4_DataBuffer& buffer) override;
  bool GetInformation(kodi::addon::InputstreamInfo& info) override;
  STREAMCODEC_PROFILE GetProfile() override { return m_codecProfile; };
  bool Transform(AP4_UI64 pts, AP4_UI32 duration, AP4_DataBuffer& buf, AP4_UI64 timescale) override;
  void SetAnnexBTransformNeeded(bool needed) { m_needAnnexBTransform = needed; }

private:
  unsigned int m_countPictureSetIds;
  STREAMCODEC_PROFILE m_codecProfile;
  bool m_needSliceInfo;
  bool m_needAnnexBTransform = false;
};
