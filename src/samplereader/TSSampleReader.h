/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SampleReader.h"
#include "demuxers/TSReader.h"

// forwards
class CAdaptiveByteStream;

class ATTR_DLL_LOCAL CTSSampleReader : public ISampleReader, public TSReader
{
public:
  CTSSampleReader(AP4_ByteStream* input, uint32_t requiredMask);

  Type GetType() const override { return Type::TS; }

  bool Initialize(SESSION::CStream* stream) override;

  void AddStreamType(INPUTSTREAM_TYPE type, int streamId);
  void SetStreamType(INPUTSTREAM_TYPE type, int streamId);
  bool RemoveStreamType(INPUTSTREAM_TYPE type);

  bool IsStarted() const override { return m_started; }
  bool EOS() const override { return m_eos; }
  uint64_t DTS() const override { return m_dts; }
  uint64_t PTS() const override { return m_pts; }
  AP4_Result Start(std::optional<uint64_t> pts) override;
  AP4_Result ReadSample() override;
  void Reset(bool bEOS) override;
  bool GetInformation(kodi::addon::InputstreamInfo& info) override
  {
    return TSReader::GetInformation(info);
  }
  bool TimeSeek(uint64_t pts) override;
  bool TimeSeekReaderPts(uint64_t pts) override;
  void SetPTSOffset(uint64_t offset) override { m_ptsOffs = offset; }
  int64_t GetPTSDiff() const override { return m_ptsDiff; }
  uint32_t GetTimeScale() const override { return 90000; }

  int GetStreamId() const override { return m_typeMap[GetStreamType()]; }
  void SetStreamId(INPUTSTREAM_TYPE type, int streamId) override;

  AP4_Size GetSampleDataSize() const override { return GetPacketSize(); }
  const AP4_Byte* GetSampleData() const override { return GetPacketData(); }
  uint64_t GetDuration() const override { return (TSReader::GetDuration() * 100) / 9; }
  bool IsEncrypted() const override { return false; }

private:
  void UpdatePts();
  bool SeekToPts(uint64_t pts, bool isManifestPts);

  uint32_t m_typeMask{0}; //Bit representation of INPUTSTREAM_TYPES
  int m_typeMap[16]{};
  uint64_t m_pts{0};
  uint64_t m_dts{0};
  uint64_t m_ptsOffs{~0ULL};
  int64_t m_ptsDiff{0};
  bool m_eos{false};
  bool m_started{false};
  CAdaptiveByteStream* m_adByteStream;
};
