/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "CodecHandler.h"

// \brief Generic audio codec handler
class ATTR_DLL_LOCAL AudioCodecHandler : public CodecHandler
{
public:
  AudioCodecHandler(AP4_SampleDescription* sd);

  bool GetInformation(kodi::addon::InputstreamInfo& info) override;

protected:
  int GetMpeg4AACProfile();
};
