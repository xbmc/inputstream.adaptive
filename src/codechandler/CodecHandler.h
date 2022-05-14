/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <bento4/Ap4.h>
#include <kodi/AddonBase.h>
#include <kodi/addon-instance/Inputstream.h>

class ATTR_DLL_LOCAL CodecHandler
{
public:
  CodecHandler(AP4_SampleDescription* sd)
    : m_sampleDescription(sd), m_naluLengthSize(0), m_pictureId(0), m_pictureIdPrev(0xFF){};
  virtual ~CodecHandler(){};

  virtual void UpdatePPSId(AP4_DataBuffer const&){};
  virtual bool GetInformation(kodi::addon::InputstreamInfo& info);
  virtual bool ExtraDataToAnnexB() { return false; };
  virtual STREAMCODEC_PROFILE GetProfile() { return STREAMCODEC_PROFILE::CodecProfileNotNeeded; };
  virtual bool Transform(AP4_UI64 pts, AP4_UI32 duration, AP4_DataBuffer& buf, AP4_UI64 timescale)
  {
    return false;
  };
  virtual bool ReadNextSample(AP4_Sample& sample, AP4_DataBuffer& buf) { return false; };
  virtual void SetPTSOffset(AP4_UI64 offset){};
  virtual bool TimeSeek(AP4_UI64 seekPos) { return true; };
  virtual void Reset(){};

  AP4_SampleDescription* m_sampleDescription;
  AP4_DataBuffer m_extraData;
  AP4_UI08 m_naluLengthSize;
  AP4_UI08 m_pictureId;
  AP4_UI08 m_pictureIdPrev;
};
