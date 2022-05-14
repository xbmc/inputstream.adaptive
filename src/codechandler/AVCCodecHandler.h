/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CodecHandler.h"

class ATTR_DLL_LOCAL AVCCodecHandler : public CodecHandler
{
public:
  AVCCodecHandler(AP4_SampleDescription* sd);
  bool ExtraDataToAnnexB() override;
  void UpdatePPSId(AP4_DataBuffer const& buffer) override;
  bool GetInformation(kodi::addon::InputstreamInfo& info) override;
  STREAMCODEC_PROFILE GetProfile() override { return m_codecProfile; };

private:
  unsigned int m_countPictureSetIds;
  STREAMCODEC_PROFILE m_codecProfile;
  bool m_needSliceInfo;
};
