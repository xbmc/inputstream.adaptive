/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "SampleReader.h"
#include "decrypters/IDecrypter.h"

#include <bento4/Ap4.h>
#include <bento4/Ap4LinearReader.h>

#include <memory>
#include <mutex>

// forwards
class CAdaptiveCencSampleDecrypter;
class CodecHandler;

// \brief Redirects AP4_LinearReader callbacks
class ATTR_DLL_LOCAL CLinearReaderCB
{
public:
  virtual AP4_Result ProcessMoof(AP4_ContainerAtom* moof,
                                 AP4_Position moof_offset,
                                 AP4_Position mdat_payload_offset,
                                 AP4_UI64 mdat_payload_size) = 0;
};

// \brief Extends the AP4_LinearReader functionality.
//        Shared use of this class between different CFragmentedSampleReaders is permitted
//        for multiplexed streams (e.g. audio embedded in video), for this reason
//        some methods are protected by mutex to avoid possible race conditions.
class ATTR_DLL_LOCAL CLinearReader : public AP4_LinearReader
{
public:
  CLinearReader(AP4_ByteStream* input, AP4_Movie* movie);

  void SetCallback(CLinearReaderCB* lReaderCB);

  // \brief Execute AP4_LinearReader::ReadNextSample
  //        and provide the ProcessMoof callback to the specified reader
  AP4_Result ExecReadNextSample(CLinearReaderCB* lReaderCB,
                                AP4_UI32 track_id,
                                AP4_Sample& sample,
                                AP4_DataBuffer& sample_data);

  // \brief Execute AP4_LinearReader::ProcessMoof
  AP4_Result ExecProcessMoof(AP4_ContainerAtom* moof,
                             AP4_Position moof_offset,
                             AP4_Position mdat_payload_offset,
                             AP4_UI64 mdat_payload_size);

  // \brief Execute AP4_LinearReader::FindTracker
  AP4_LinearReader::Tracker* FindTracker(AP4_UI32 track_id);

  // \brief Execute AP4_LinearReader::GetSample
  AP4_Result GetSample(AP4_UI32 track_id, AP4_Sample& sample, AP4_Ordinal sample_index);
  
  // \brief Execute AP4_LinearReader::Reset
  void Reset();

  // \brief Get AP4_LinearReader::m_FragmentStream
  AP4_ByteStream* GetByteStream();

  // \brief Get AP4_LinearReader::m_Movie
  AP4_Movie& GetMovie();

  AP4_Result SeekSample(AP4_UI32 track_id,
                        AP4_UI64 ts,
                        AP4_Ordinal& sample_index,
                        bool preceedingSync);

protected:
  // AP4_LinearReader implementations
  AP4_Result ProcessMoof(AP4_ContainerAtom* moof,
                         AP4_Position moof_offset,
                         AP4_Position mdat_payload_offset,
                         AP4_UI64 mdat_payload_size) override;

  CLinearReaderCB* m_lReaderCB{nullptr};
  std::mutex m_readerMtx;
};

class ATTR_DLL_LOCAL CFragmentedSampleReader : public ISampleReader, public CLinearReaderCB
{
public:
  CFragmentedSampleReader(AP4_ByteStream* input, AP4_Movie* movie, AP4_Track* track);
  CFragmentedSampleReader(std::shared_ptr<CLinearReader> lReader, AP4_Track* track);
  ~CFragmentedSampleReader();

  Type GetType() const override { return Type::FMP4; }

  virtual bool Initialize(SESSION::CStream* stream) override;
  virtual void SetDecrypter(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> ssd,
                            const DRM::DecrypterCapabilites& dcaps,
                            const std::vector<uint8_t>& defaultKid) override;

  AP4_Result Start(bool& bStarted) override;
  AP4_Result ReadSample() override;
  void Reset(bool bEOS) override;
  bool EOS() const override { return m_eos; }
  bool IsStarted() const override { return m_started; }
  uint64_t DTS() const override { return m_dts; }
  uint64_t PTS() const override { return m_pts; }
  AP4_Size GetSampleDataSize() const override { return m_sampleData.GetDataSize(); }
  const AP4_Byte* GetSampleData() const override { return m_sampleData.GetData(); }
  uint64_t GetDuration() const override;
  bool IsEncrypted() const override;
  bool GetInformation(kodi::addon::InputstreamInfo& info) override;
  bool TimeSeek(uint64_t pts, bool preceeding) override;
  void SetPTSOffset(uint64_t offset) override;
  int64_t GetPTSDiff() const override { return m_ptsDiff; }
  bool GetFragmentInfo(uint64_t& duration) override;
  uint32_t GetTimeScale() const override { return m_track->GetMediaTimeScale(); }
  CryptoInfo GetReaderCryptoInfo() const override { return m_readerCryptoInfo; }

  /*!
   * \brief Create a new reader by sharing the same data reader and segment management (AdaptiveStream).
   *        Use intended for multiplexed tracks (e.g. audio included on video stream).
   */
  std::unique_ptr<ISampleReader> CreateReaderByTrack();

protected:
  // CLinearReaderCB implementation
  AP4_Result ProcessMoof(AP4_ContainerAtom* moof,
                         AP4_Position moof_offset,
                         AP4_Position mdat_payload_offset,
                         AP4_UI64 mdat_payload_size) override;

private:
  void UpdateSampleDescription();
  void ParseTrafTfrf(AP4_UuidAtom* uuidAtom);

  AP4_Track* m_track;
  AP4_UI32 m_poolId{0};
  AP4_UI32 m_sampleDescIndex{1};
  DRM::DecrypterCapabilites m_decrypterCaps;
  unsigned int m_failCount{0};
  bool m_bSampleDescChanged{false};
  bool m_eos{false};
  bool m_started{false};
  int64_t m_dts{0};
  int64_t m_pts{0};
  int64_t m_ptsDiff{0};
  AP4_UI64 m_ptsOffs{~0ULL};
  uint64_t m_timeBaseExt{0};
  uint64_t m_timeBaseInt{0};
  AP4_Sample m_sample;
  AP4_DataBuffer m_sampleData;
  CodecHandler* m_codecHandler{nullptr};
  std::vector<uint8_t> m_defaultKey;
  AP4_ProtectedSampleDescription* m_protectedDesc{nullptr};
  std::shared_ptr<Adaptive_CencSingleSampleDecrypter> m_singleSampleDecryptor{nullptr};
  std::unique_ptr<CAdaptiveCencSampleDecrypter> m_decrypter;
  CryptoInfo m_readerCryptoInfo{};

  std::shared_ptr<CLinearReader> m_lReader;
};
