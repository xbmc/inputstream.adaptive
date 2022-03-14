/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "CodecHandler.h"

class ATTR_DLL_LOCAL WebVTTCodecHandler : public CodecHandler
{
public:
  WebVTTCodecHandler(AP4_SampleDescription* sd, bool asFile);

  bool Transform(AP4_UI64 pts, AP4_UI32 duration, AP4_DataBuffer& buf, AP4_UI64 timescale) override;

  bool ReadNextSample(AP4_Sample& sample, AP4_DataBuffer& buf) override;
  void SetPTSOffset(AP4_UI64 offset) override { m_ptsOffset = offset; }
  bool TimeSeek(AP4_UI64 seekPos) override { return true; }
  void Reset() override {}

private:
  AP4_UI64 m_ptsOffset;
  AP4_DataBuffer m_data;
  AP4_UI64 m_pts{0};
  AP4_UI32 m_duration{0};
};
