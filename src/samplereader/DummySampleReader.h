/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SampleReader.h"

class ATTR_DLL_LOCAL CDummySampleReader : public ISampleReader
{
public:
  virtual ~CDummySampleReader() = default;
  bool EOS() const override { return false; }
  uint64_t DTS() const override { return STREAM_NOPTS_VALUE; }
  uint64_t PTS() const override { return STREAM_NOPTS_VALUE; }
  AP4_Result Start(bool& bStarted) override { return AP4_SUCCESS; }
  AP4_Result ReadSample() override { return AP4_SUCCESS; }
  void Reset(bool bEOS) override {}
  bool GetInformation(kodi::addon::InputstreamInfo& info) override { return false; }
  bool TimeSeek(uint64_t pts, bool preceeding) override { return false; }
  void SetPTSOffset(uint64_t offset) override {}
  int64_t GetPTSDiff() const override { return 0; }
  bool GetNextFragmentInfo(uint64_t& ts, uint64_t& dur) override { return false; }
  uint32_t GetTimeScale() const override { return 1; }
  AP4_UI32 GetStreamId() const override { return 0; }
  AP4_Size GetSampleDataSize() const override { return 0; }
  const AP4_Byte* GetSampleData() const override { return nullptr; }
  uint64_t GetDuration() const override { return 0; }
  bool IsEncrypted() const override { return false; }
  void AddStreamType(INPUTSTREAM_TYPE type, uint32_t sid) override {};
  void SetStreamType(INPUTSTREAM_TYPE type, uint32_t sid) override {};
  bool RemoveStreamType(INPUTSTREAM_TYPE type) override { return true; };
  bool IsStarted() const override { return true; }
};
