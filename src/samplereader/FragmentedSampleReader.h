/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "codechandler/CodecHandler.h"
#include "common/AdaptiveCencSampleDecrypter.h"
#include "common/AdaptiveDecrypter.h"
#include "decrypters/IDecrypter.h"
#include "utils/log.h"
#include "SampleReader.h"

class ATTR_DLL_LOCAL CFragmentedSampleReader : public ISampleReader, public AP4_LinearReader
{
public:
  CFragmentedSampleReader(AP4_ByteStream* input,
                         AP4_Movie* movie,
                         AP4_Track* track,
                         AP4_UI32 streamId,
                         Adaptive_CencSingleSampleDecrypter* ssd,
                         const DRM::IDecrypter::DecrypterCapabilites& dcaps);

  ~CFragmentedSampleReader();

  AP4_Result Start(bool& bStarted) override;
  AP4_Result ReadSample() override;
  void Reset(bool bEOS) override;
  bool EOS() const override { return m_eos; }
  bool IsStarted() const override { return m_started; }
  uint64_t DTS() const override { return m_dts; }
  uint64_t PTS() const override { return m_pts; }
  AP4_UI32 GetStreamId() const override { return m_streamId; }
  AP4_Size GetSampleDataSize() const override { return m_sampleData.GetDataSize(); }
  const AP4_Byte* GetSampleData() const override { return m_sampleData.GetData(); }
  uint64_t GetDuration() const override;
  bool IsEncrypted() const override;
  bool GetInformation(kodi::addon::InputstreamInfo& info) override;
  bool TimeSeek(uint64_t pts, bool preceeding) override;
  void SetPTSOffset(uint64_t offset) override;
  uint64_t GetStartPTS() const override { return m_startPts; }
  void SetStartPTS(uint64_t pts) override { m_startPts = pts; }
  int64_t GetPTSDiff() const override { return m_ptsDiff; }
  bool GetFragmentInfo(uint64_t& duration) override;
  uint32_t GetTimeScale() const override { return m_track->GetMediaTimeScale(); }
  CryptoInfo GetReaderCryptoInfo() const override { return m_readerCryptoInfo; }

  static const AP4_UI32 TRACKID_UNKNOWN = -1;

protected:
  AP4_Result ProcessMoof(AP4_ContainerAtom* moof,
                         AP4_Position moof_offset,
                         AP4_Position mdat_payload_offset,
                         AP4_UI64 mdat_payload_size) override;

private:
  void UpdateSampleDescription();
  void ParseTrafTfrf(AP4_UuidAtom* uuidAtom);

  AP4_Track* m_track;
  AP4_UI32 m_poolId{0};
  AP4_UI32 m_streamId;
  AP4_UI32 m_sampleDescIndex{1};
  DRM::IDecrypter::DecrypterCapabilites m_decrypterCaps;
  unsigned int m_failCount{0};
  bool m_bSampleDescChanged{false};
  bool m_eos{false};
  bool m_started{false};
  int64_t m_dts{0};
  int64_t m_pts{0};
  int64_t m_ptsDiff{0};
  uint64_t m_startPts{STREAM_NOPTS_VALUE};
  AP4_UI64 m_ptsOffs{~0ULL};
  uint64_t m_timeBaseExt;
  uint64_t m_timeBaseInt;
  AP4_Sample m_sample;
  AP4_DataBuffer m_encrypted;
  AP4_DataBuffer m_sampleData;
  CodecHandler* m_codecHandler{nullptr};
  std::vector<uint8_t> m_defaultKey;
  AP4_ProtectedSampleDescription* m_protectedDesc{nullptr};
  Adaptive_CencSingleSampleDecrypter* m_singleSampleDecryptor;
  CAdaptiveCencSampleDecrypter* m_decrypter{nullptr};
  CryptoInfo m_readerCryptoInfo{};
};
