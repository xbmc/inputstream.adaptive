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
  CSubtitleSampleReader() = default;

  Type GetType() const override { return Type::Subtitles; }

  virtual bool Initialize(SESSION::CStream* stream) override;
  bool IsStarted() const override { return m_started; }
  bool EOS() const override { return m_eos; }
  bool IsReady() override;
  uint64_t DTS() const override { return m_pts; }
  uint64_t PTS() const override { return m_pts; }
  AP4_Result Start(bool& bStarted) override;
  AP4_Result ReadSample() override;
  void Reset(bool bEOS) override;
  bool GetInformation(kodi::addon::InputstreamInfo& info) override;
  bool TimeSeek(uint64_t pts) override;
  void SetPTSOffset(uint64_t offset) override { m_ptsOffset = offset; }
  int64_t GetPTSDiff() const override { return 0; }
  uint32_t GetTimeScale() const override { return 1000; }
  AP4_Size GetSampleDataSize() const override { return m_sampleData.GetDataSize(); }
  const AP4_Byte* GetSampleData() const override { return m_sampleData.GetData(); }
  uint64_t GetDuration() const override { return m_sample.GetDuration() * 1000; }
  bool IsEncrypted() const override { return false; }

private:
  bool InitializeFile(std::string url);

  uint64_t m_pts{0};
  uint64_t m_ptsOffset{0};
  bool m_eos{false};
  bool m_started{false};
  std::unique_ptr<CodecHandler> m_codecHandler;
  AP4_Sample m_sample;
  AP4_DataBuffer m_sampleData;
  CAdaptiveByteStream* m_adByteStream{nullptr};
  adaptive::AdaptiveStream* m_adStream{nullptr};
  const AP4_Size m_segmentChunkSize = 16384; // 16kb
};
