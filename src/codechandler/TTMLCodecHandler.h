/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CodecHandler.h"
#include "ttml/TTML.h"

class ATTR_DLL_LOCAL TTMLCodecHandler : public CodecHandler
{
public:
  TTMLCodecHandler(AP4_SampleDescription* sd) : CodecHandler(sd), m_ptsOffset(0){};

  bool Transform(AP4_UI64 pts, AP4_UI32 duration, AP4_DataBuffer& buf, AP4_UI64 timescale) override;
  bool ReadNextSample(AP4_Sample& sample, AP4_DataBuffer& buf) override;
  void SetPTSOffset(AP4_UI64 offset) override { m_ptsOffset = offset; };
  bool TimeSeek(AP4_UI64 seekPos) override { return m_ttml.TimeSeek(seekPos); };
  void Reset() override { m_ttml.Reset(); }

private:
  TTML2SRT m_ttml;
  AP4_UI64 m_ptsOffset;
};
