/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SampleReader.h"

#include "codechandler/CodecHandler.h"

#include <memory>
#include <string_view>

// forwards
class CAdaptiveByteStream;
namespace SESSION
{
class CStream;
}
namespace adaptive
{
class AdaptiveStream;
}

class ATTR_DLL_LOCAL CSubtitleSampleReader : public ISampleReader
{
public:
  CSubtitleSampleReader(AP4_UI32 streamId);

  virtual bool Initialize(SESSION::CStream* stream) override;
  bool IsStarted() const override { return m_started; }
  bool EOS() const override { return m_eos; }
  uint64_t DTS() const override { return m_pts; }
  uint64_t PTS() const override { return m_pts; }
  AP4_Result Start(bool& bStarted) override;
  AP4_Result ReadSample() override;
  void Reset(bool bEOS) override;
  bool GetInformation(kodi::addon::InputstreamInfo& info) override;
  bool TimeSeek(uint64_t pts, bool preceeding) override;
  void SetPTSOffset(uint64_t offset) override { m_ptsOffset = offset; }
  uint64_t GetStartPTS() const override { return m_startPts; }
  void SetStartPTS(uint64_t pts) override { m_startPts = pts; }
  int64_t GetPTSDiff() const override { return m_ptsDiff; }
  uint32_t GetTimeScale() const override { return 1000; }
  AP4_UI32 GetStreamId() const override { return m_streamId; }
  AP4_Size GetSampleDataSize() const override { return m_sampleData.GetDataSize(); }
  const AP4_Byte* GetSampleData() const override { return m_sampleData.GetData(); }
  uint64_t GetDuration() const override { return m_sample.GetDuration() * 1000; }
  bool IsEncrypted() const override { return false; }

private:
  bool InitializeFile(std::string url);

  uint64_t m_pts{0};
  uint64_t m_ptsOffset{0};
  uint64_t m_ptsDiff{0};
  uint64_t m_startPts{STREAM_NOPTS_VALUE};
  AP4_UI32 m_streamId;
  bool m_eos{false};
  bool m_started{false};
  std::unique_ptr<CodecHandler> m_codecHandler;
  AP4_Sample m_sample;
  AP4_DataBuffer m_sampleData;
  CAdaptiveByteStream* m_adByteStream{nullptr};
  adaptive::AdaptiveStream* m_adStream{nullptr};
  const AP4_Size m_segmentChunkSize = 16384; // 16kb
};
