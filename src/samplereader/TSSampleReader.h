/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "../AdaptiveByteStream.h"
#include "../TSReader.h"
#include "SampleReader.h"

class ATTR_DLL_LOCAL CTSSampleReader : public ISampleReader, public TSReader
{
public:
  CTSSampleReader(AP4_ByteStream* input,
                  INPUTSTREAM_TYPE type,
                  AP4_UI32 streamId,
                  uint32_t requiredMask);

  bool Initialize() override;
  void AddStreamType(INPUTSTREAM_TYPE type, uint32_t sid) override;
  void SetStreamType(INPUTSTREAM_TYPE type, uint32_t sid) override;
  bool RemoveStreamType(INPUTSTREAM_TYPE type) override;
  bool IsStarted() const override { return m_started; }
  bool EOS() const override { return m_eos; }
  uint64_t DTS() const override { return m_dts; }
  uint64_t PTS() const override { return m_pts; }
  AP4_Result Start(bool& bStarted) override;
  AP4_Result ReadSample() override;
  void Reset(bool bEOS) override;
  bool GetInformation(kodi::addon::InputstreamInfo& info) override
  {
    return TSReader::GetInformation(info);
  }
  bool TimeSeek(uint64_t pts, bool preceeding) override;
  void SetPTSOffset(uint64_t offset) override { m_ptsOffs = offset; }
  int64_t GetPTSDiff() const override { return m_ptsDiff; }
  bool GetNextFragmentInfo(uint64_t& ts, uint64_t& dur) override { return false; }
  uint32_t GetTimeScale() const override { return 90000; }
  AP4_UI32 GetStreamId() const override { return m_typeMap[GetStreamType()]; }
  AP4_Size GetSampleDataSize() const override { return GetPacketSize(); }
  const AP4_Byte* GetSampleData() const override { return GetPacketData(); }
  uint64_t GetDuration() const override { return (TSReader::GetDuration() * 100) / 9; }
  bool IsEncrypted() const override { return false; }

private:
  uint32_t m_typeMask; //Bit representation of INPUTSTREAM_TYPES
  uint32_t m_typeMap[16];
  uint64_t m_pts{0};
  uint64_t m_dts{0};
  uint64_t m_ptsOffs{~0ULL};
  int64_t m_ptsDiff{0};
  bool m_eos{false};
  bool m_started{false};
  CAdaptiveByteStream* m_adByteStream;
};
