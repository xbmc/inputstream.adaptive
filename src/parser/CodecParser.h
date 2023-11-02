/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include <bento4/Ap4Ac3Parser.h>
#include <bento4/Ap4AdtsParser.h>
#include <bento4/Ap4Eac3Parser.h>
#include <kodi/addon-instance/Inputstream.h>

namespace adaptive
{

constexpr AP4_UI32 AP4_ADTS_HEADER_SIZE = 7;
constexpr AP4_UI32 AP4_ADTS_SYNC_MASK = 0xFFF6;
constexpr AP4_UI32 AP4_ADTS_SYNC_PATTERN = 0xFFF0;
constexpr AP4_UI32 AP4_AC3_SYNC_PATTERN = 0x0B77;
constexpr AP4_UI32 AP4_AC4_SYNC_MASK = 0xFFF0;
constexpr AP4_UI32 AP4_AC4_SYNC_PATTERN = 0x0AC40;

enum class AdtsType
{
  NONE,
  AAC,
  AC3,
  EAC3,
  AC4
};

class ATTR_DLL_LOCAL CAdaptiveAdtsParser : public AP4_AdtsParser
{
public:
  CAdaptiveAdtsParser() {}
  AP4_Result FindFrameHeader(AP4_AacFrame& frame);
};

class ATTR_DLL_LOCAL CAdaptiveAc3Parser : public AP4_Ac3Parser
{
public:
  CAdaptiveAc3Parser() {}
  AP4_Result FindFrameHeader(AP4_Ac3Frame& frame);
};

class ATTR_DLL_LOCAL CAdaptiveEac3Parser : public AP4_Eac3Parser
{
public:
  CAdaptiveEac3Parser() {}
  AP4_Result FindFrameHeader(AP4_Eac3Frame& frame);
};

class ATTR_DLL_LOCAL CAdaptiveAdtsHeaderParser
{
public:
  static AdtsType GetAdtsType(AP4_ByteStream* stream);
};

} // namespace adaptive
