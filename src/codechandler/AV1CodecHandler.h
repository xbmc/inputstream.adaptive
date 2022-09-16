/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "CodecHandler.h"

class ATTR_DLL_LOCAL AV1CodecHandler : public CodecHandler
{
public:
  AV1CodecHandler(AP4_SampleDescription* sd);

  bool GetInformation(kodi::addon::InputstreamInfo& info) override;
  STREAMCODEC_PROFILE GetProfile() override { return m_codecProfile; }

private:
  STREAMCODEC_PROFILE m_codecProfile;
};
