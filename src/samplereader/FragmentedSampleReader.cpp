/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "FragmentedSampleReader.h"

#include "../AdaptiveByteStream.h"
#include "../codechandler/AV1CodecHandler.h"
#include "../codechandler/AVCCodecHandler.h"
#include "../codechandler/HEVCCodecHandler.h"
#include "../codechandler/MPEGCodecHandler.h"
#include "../codechandler/TTMLCodecHandler.h"
#include "../codechandler/VP9CodecHandler.h"
#include "../codechandler/WebVTTCodecHandler.h"
#include "../utils/log.h"

namespace
{
constexpr uint8_t SMOOTHSTREAM_TFRFBOX_UUID[] = {0xd4, 0x80, 0x7e, 0xf2, 0xca, 0x39, 0x46, 0x95,
                                                 0x8e, 0x54, 0x26, 0xcb, 0x9e, 0x46, 0xa7, 0x9f};
} // unnamed namespace


CFragmentedSampleReader::CFragmentedSampleReader(AP4_ByteStream* input,
                                                 AP4_Movie* movie,
                                                 AP4_Track* track,
                                                 AP4_UI32 streamId,
                                                 Adaptive_CencSingleSampleDecrypter* ssd,
                                                 const SSD::SSD_DECRYPTER::SSD_CAPS& dcaps)
  : AP4_LinearReader{*movie, input},
    m_track{track},
    m_streamId{streamId},
    m_decrypterCaps{dcaps},
    m_singleSampleDecryptor{ssd}
{
  EnableTrack(m_track->GetId());

  AP4_SampleDescription* desc{m_track->GetSampleDescription(0)};
  if (desc->GetType() == AP4_SampleDescription::TYPE_PROTECTED)
  {
    m_protectedDesc = static_cast<AP4_ProtectedSampleDescription*>(desc);

    AP4_ContainerAtom* schi;
    if (m_protectedDesc->GetSchemeInfo() &&
        (schi = m_protectedDesc->GetSchemeInfo()->GetSchiAtom()))
    {
      AP4_TencAtom* tenc(AP4_DYNAMIC_CAST(AP4_TencAtom, schi->GetChild(AP4_ATOM_TYPE_TENC, 0)));
      if (tenc)
        m_defaultKey = tenc->GetDefaultKid();
      else
      {
        AP4_PiffTrackEncryptionAtom* piff(AP4_DYNAMIC_CAST(
            AP4_PiffTrackEncryptionAtom, schi->GetChild(AP4_UUID_PIFF_TRACK_ENCRYPTION_ATOM, 0)));
        if (piff)
          m_defaultKey = piff->GetDefaultKid();
      }
    }
  }

  if (m_singleSampleDecryptor)
    m_poolId = m_singleSampleDecryptor->AddPool();

  m_timeBaseExt = STREAM_TIME_BASE;
  m_timeBaseInt = m_track->GetMediaTimeScale();
  if (m_timeBaseInt == 0)
  {
    LOG::LogF(LOGWARNING, "Unable to get track media timescale value.");
    m_timeBaseInt = 1;
  }

  // remove unneeded trailing zeroes
  while (m_timeBaseExt > 1)
  {
    if ((m_timeBaseInt / 10) * 10 == m_timeBaseInt)
    {
      m_timeBaseExt /= 10;
      m_timeBaseInt /= 10;
    }
    else
      break;
  }

  //We need this to fill extradata
  UpdateSampleDescription();
}

CFragmentedSampleReader::~CFragmentedSampleReader()
{
  if (m_singleSampleDecryptor)
    m_singleSampleDecryptor->RemovePool(m_poolId);
  delete m_decrypter;
  delete m_codecHandler;
}

AP4_Result CFragmentedSampleReader::Start(bool& bStarted)
{
  bStarted = false;
  if (m_started)
    return AP4_SUCCESS;

  m_started = true;
  bStarted = true;
  return ReadSample();
}

AP4_Result CFragmentedSampleReader::ReadSample()
{
  AP4_Result result;
  if (!m_codecHandler || !m_codecHandler->ReadNextSample(m_sample, m_sampleData))
  {
    bool useDecryptingDecoder =
        m_protectedDesc &&
        (m_decrypterCaps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH) != 0;
    bool decrypterPresent{m_decrypter != nullptr};
    if (AP4_FAILED(result = ReadNextSample(m_track->GetId(), m_sample,
                                           (m_decrypter || useDecryptingDecoder) ? m_encrypted
                                                                                 : m_sampleData)))
    {
      if (result == AP4_ERROR_EOS)
      {
        auto adByteStream = dynamic_cast<CAdaptiveByteStream*>(m_FragmentStream);
        if (!adByteStream)
        {
          LOG::LogF(LOGERROR, "Fragment stream cannot be casted to AdaptiveByteStream");
          m_eos = true;
        }
        else
        {
          if (adByteStream->waitingForSegment())
          {
            m_sampleData.SetDataSize(0);
          }
          else
          {
            m_eos = true;
          }
        }
      }
      return result;
    }

    //AP4_AvcSequenceParameterSet sps;
    //AP4_AvcFrameParser::ParseFrameForSPS(m_sampleData.GetData(), m_sampleData.GetDataSize(), 4, sps);

    //Protection could have changed in ProcessMoof
    if (!decrypterPresent && m_decrypter != nullptr && !useDecryptingDecoder)
      m_encrypted.SetData(m_sampleData.GetData(), m_sampleData.GetDataSize());
    else if (decrypterPresent && m_decrypter == nullptr && !useDecryptingDecoder)
      m_sampleData.SetData(m_encrypted.GetData(), m_encrypted.GetDataSize());

    if (m_decrypter)
    {
      // Make sure that the decrypter is NOT allocating memory!
      // If decrypter and addon are compiled with different DEBUG / RELEASE
      // options freeing HEAP memory will fail.
      m_sampleData.Reserve(m_encrypted.GetDataSize() + 4096);
      if (AP4_FAILED(result =
                         m_decrypter->DecryptSampleData(m_poolId, m_encrypted, m_sampleData, NULL)))
      {
        LOG::Log(LOGERROR, "Decrypt Sample returns failure!");
        if (++m_failCount > 50)
        {
          Reset(true);
          return result;
        }
        else
        {
          m_sampleData.SetDataSize(0);
        }
      }
      else
      {
        m_failCount = 0;
      }
    }
    else if (useDecryptingDecoder)
    {
      m_sampleData.Reserve(m_encrypted.GetDataSize() + 1024);
      m_singleSampleDecryptor->DecryptSampleData(m_poolId, m_encrypted, m_sampleData, nullptr, 0,
                                                 nullptr, nullptr);
    }

    if (m_codecHandler->Transform(m_sample.GetDts(), m_sample.GetDuration(), m_sampleData,
                                  m_track->GetMediaTimeScale()))
    {
      m_codecHandler->ReadNextSample(m_sample, m_sampleData);
    }
  }

  m_dts = (m_sample.GetDts() * m_timeBaseExt) / m_timeBaseInt;
  m_pts = (m_sample.GetCts() * m_timeBaseExt) / m_timeBaseInt;
  
  if (m_startPts == STREAM_NOPTS_VALUE)
    SetStartPTS(m_pts - GetPTSDiff());

  m_codecHandler->UpdatePPSId(m_sampleData);

  return AP4_SUCCESS;
}

void CFragmentedSampleReader::Reset(bool bEOS)
{
  AP4_LinearReader::Reset();
  m_eos = bEOS;
  if (m_codecHandler)
    m_codecHandler->Reset();
}

uint64_t CFragmentedSampleReader::GetDuration() const
{
  return (m_sample.GetDuration() * m_timeBaseExt) / m_timeBaseInt;
}

bool CFragmentedSampleReader::IsEncrypted() const
{
  return (m_decrypterCaps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH) != 0 &&
         m_decrypter != nullptr;
}

bool CFragmentedSampleReader::GetInformation(kodi::addon::InputstreamInfo& info)
{
  if (!m_codecHandler)
    return false;

  bool edChanged(false);
  if (m_bSampleDescChanged && m_codecHandler->m_extraData.GetDataSize() &&
      !info.CompareExtraData(m_codecHandler->m_extraData.GetData(),
                             m_codecHandler->m_extraData.GetDataSize()))
  {
    info.SetExtraData(m_codecHandler->m_extraData.GetData(),
                      m_codecHandler->m_extraData.GetDataSize());
    edChanged = true;
  }

  AP4_SampleDescription* desc(m_track->GetSampleDescription(0));
  if (desc->GetType() == AP4_SampleDescription::TYPE_MPEG)
  {
    switch (static_cast<AP4_MpegSampleDescription*>(desc)->GetObjectTypeId())
    {
      case AP4_OTI_MPEG4_AUDIO:
      case AP4_OTI_MPEG2_AAC_AUDIO_MAIN:
      case AP4_OTI_MPEG2_AAC_AUDIO_LC:
      case AP4_OTI_MPEG2_AAC_AUDIO_SSRP:
        info.SetCodecName("aac");
        break;
      case AP4_OTI_DTS_AUDIO:
      case AP4_OTI_DTS_HIRES_AUDIO:
      case AP4_OTI_DTS_MASTER_AUDIO:
      case AP4_OTI_DTS_EXPRESS_AUDIO:
        info.SetCodecName("dca");
        break;
      case AP4_OTI_AC3_AUDIO:
        info.SetCodecName("ac3");
        break;
      case AP4_OTI_EAC3_AUDIO:
        info.SetCodecName("eac3");
        break;
    }
  }

  m_bSampleDescChanged = false;

  if (m_codecHandler->GetInformation(info))
    return true;

  return edChanged;
}

bool CFragmentedSampleReader::TimeSeek(uint64_t pts, bool preceeding)
{
  AP4_Ordinal sampleIndex;
  AP4_UI64 seekPos(static_cast<AP4_UI64>((pts * m_timeBaseInt) / m_timeBaseExt));
  if (AP4_SUCCEEDED(SeekSample(m_track->GetId(), seekPos, sampleIndex, preceeding)))
  {
    if (m_decrypter)
      m_decrypter->SetSampleIndex(sampleIndex);

    if (m_codecHandler)
      m_codecHandler->TimeSeek(seekPos);

    m_started = true;
    return AP4_SUCCEEDED(ReadSample());
  }
  return false;
}

void CFragmentedSampleReader::SetPTSOffset(uint64_t offset)
{
  FindTracker(m_track->GetId())->m_NextDts = (offset * m_timeBaseInt) / m_timeBaseExt;
  m_ptsOffs = offset;

  if (m_codecHandler)
    m_codecHandler->SetPTSOffset((offset * m_timeBaseInt) / m_timeBaseExt);
}

bool CFragmentedSampleReader::GetNextFragmentInfo(uint64_t& ts, uint64_t& dur)
{
  if (m_nextDuration)
  {
    dur = m_nextDuration;
    ts = m_nextTimestamp;
  }
  else
  {
    auto fragSampleTable =
        dynamic_cast<AP4_FragmentSampleTable*>(FindTracker(m_track->GetId())->m_SampleTable);
    if (fragSampleTable)
    {
      dur = fragSampleTable->GetDuration();
      ts = 0;
    }
    else
    {
      LOG::LogF(LOGERROR, "Can't get FragmentSampleTable from track %u", m_track->GetId());
      return false;
    }
  }
  return true;
}

AP4_Result CFragmentedSampleReader::ProcessMoof(AP4_ContainerAtom* moof,
                                                AP4_Position moof_offset,
                                                AP4_Position mdat_payload_offset,
                                                AP4_UI64 mdat_payload_size)
{
  AP4_MovieFragment fragment =
      AP4_MovieFragment(AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof->Clone()));
  AP4_Array<AP4_UI32> ids;
  fragment.GetTrackIds(ids);
  if (ids.ItemCount() == 1)
  {
    // For prefixed initialization (usually ISM) we don't yet know the
    // proper track id, let's find it now
    if (m_track->GetId() == TRACKID_UNKNOWN)
    {
      m_track->SetId(ids[0]);
      LOG::LogF(LOGDEBUG, "Track ID changed from UNKNOWN to %u", ids[0]);
    }
    else if (ids[0] != m_track->GetId())
    {
      LOG::LogF(LOGDEBUG, "Track ID does not match! Expected: %u Got: %u", m_track->GetId(), ids[0]);
      return AP4_ERROR_NO_SUCH_ITEM;
    }
  }

  AP4_Result result;

  if (AP4_SUCCEEDED((result = AP4_LinearReader::ProcessMoof(moof, moof_offset, mdat_payload_offset,
                                                            mdat_payload_size))))
  {
    AP4_ContainerAtom* traf =
        AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof->GetChild(AP4_ATOM_TYPE_TRAF, 0));

    //For ISM Livestreams we have an UUID atom with one / more following fragment durations
    m_nextDuration = 0;
    m_nextTimestamp = 0;
    AP4_Atom* atom{nullptr};
    unsigned int atom_pos{0};

    while ((atom = traf->GetChild(AP4_ATOM_TYPE_UUID, atom_pos++)) != nullptr)
    {
      AP4_UuidAtom* uuid_atom{AP4_DYNAMIC_CAST(AP4_UuidAtom, atom)};
      if (memcmp(uuid_atom->GetUuid(), SMOOTHSTREAM_TFRFBOX_UUID, 16) == 0)
      {
        //verison(8) + flags(24) + numpairs(8) + pairs(ts(64)/dur(64))*numpairs
        const AP4_DataBuffer& buf(AP4_DYNAMIC_CAST(AP4_UnknownUuidAtom, uuid_atom)->GetData());
        if (buf.GetDataSize() >= 21)
        {
          const uint8_t* data(buf.GetData());
          m_nextTimestamp = AP4_BytesToUInt64BE(data + 5);
          m_nextDuration = AP4_BytesToUInt64BE(data + 13);
        }
        break;
      }
    }

    //Check if the sample table description has changed
    AP4_TfhdAtom* tfhd{AP4_DYNAMIC_CAST(AP4_TfhdAtom, traf->GetChild(AP4_ATOM_TYPE_TFHD, 0))};
    if ((tfhd && tfhd->GetSampleDescriptionIndex() != m_sampleDescIndex) ||
        (!tfhd && (m_sampleDescIndex = 1)))
    {
      m_sampleDescIndex = tfhd->GetSampleDescriptionIndex();
      UpdateSampleDescription();
    }

    //Correct PTS
    AP4_Sample sample;
    if (~m_ptsOffs)
    {
      if (AP4_SUCCEEDED(GetSample(m_track->GetId(), sample, 0)))
      {
        m_pts = m_dts = (sample.GetCts() * m_timeBaseExt) / m_timeBaseInt;
        m_ptsDiff = m_pts - m_ptsOffs;
      }
      m_ptsOffs = ~0ULL;
    }

    if (m_protectedDesc)
    {
      //Setup the decryption
      AP4_CencSampleInfoTable* sample_table{nullptr};
      AP4_UI32 algorithm_id = 0;

      delete m_decrypter;
      m_decrypter = 0;

      AP4_ContainerAtom* traf =
          AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof->GetChild(AP4_ATOM_TYPE_TRAF, 0));

      if (!m_protectedDesc || !traf)
        return AP4_ERROR_INVALID_FORMAT;

      bool reset_iv(false);
      if (AP4_FAILED(result = AP4_CencSampleInfoTable::Create(m_protectedDesc, traf, algorithm_id,
                                                              reset_iv, *m_FragmentStream,
                                                              moof_offset, sample_table)))
        // we assume unencrypted fragment here
        goto SUCCESS;

      if (!m_singleSampleDecryptor)
        return AP4_ERROR_INVALID_PARAMETERS;

      m_decrypter = new CAdaptiveCencSampleDecrypter(m_singleSampleDecryptor, sample_table);

      // Inform decrypter of pattern decryption (CBCS)
      AP4_UI32 schemeType = m_protectedDesc->GetSchemeType();
      if (schemeType == AP4_PROTECTION_SCHEME_TYPE_CENC ||
          schemeType == AP4_PROTECTION_SCHEME_TYPE_PIFF ||
          schemeType == AP4_PROTECTION_SCHEME_TYPE_CBCS)
      {
        m_readerCryptoInfo.m_cryptBlocks = sample_table->GetCryptByteBlock();
        m_readerCryptoInfo.m_skipBlocks = sample_table->GetSkipByteBlock();

        if (schemeType == AP4_PROTECTION_SCHEME_TYPE_CENC ||
            schemeType == AP4_PROTECTION_SCHEME_TYPE_PIFF)
          m_readerCryptoInfo.m_mode = CryptoMode::AES_CTR;
        else
          m_readerCryptoInfo.m_mode = CryptoMode::AES_CBC;
      }
      else if (schemeType == AP4_PROTECTION_SCHEME_TYPE_CBC1 ||
               schemeType == AP4_PROTECTION_SCHEME_TYPE_CENS)
      {
        LOG::LogF(LOGERROR, "Protection scheme %u not implemented.", schemeType);
      }
    }
  }
SUCCESS:
  if (m_singleSampleDecryptor && m_codecHandler)
  {
     m_singleSampleDecryptor->SetFragmentInfo(
        m_poolId, m_defaultKey, m_codecHandler->m_naluLengthSize, m_codecHandler->m_extraData,
        m_decrypterCaps.flags, m_readerCryptoInfo);
  }
  return AP4_SUCCESS;
}

void CFragmentedSampleReader::UpdateSampleDescription()
{
  if (m_codecHandler)
    delete m_codecHandler;
  m_codecHandler = 0;
  m_bSampleDescChanged = true;

  AP4_SampleDescription* desc(m_track->GetSampleDescription(m_sampleDescIndex - 1));
  if (desc->GetType() == AP4_SampleDescription::TYPE_PROTECTED)
  {
    m_protectedDesc = static_cast<AP4_ProtectedSampleDescription*>(desc);
    desc = m_protectedDesc->GetOriginalSampleDescription();
  }
  LOG::Log(LOGDEBUG, "UpdateSampleDescription: codec %d", desc->GetFormat());
  switch (desc->GetFormat())
  {
    case AP4_SAMPLE_FORMAT_AVC1:
    case AP4_SAMPLE_FORMAT_AVC2:
    case AP4_SAMPLE_FORMAT_AVC3:
    case AP4_SAMPLE_FORMAT_AVC4:
      m_codecHandler = new AVCCodecHandler(desc);
      break;
    case AP4_SAMPLE_FORMAT_HEV1:
    case AP4_SAMPLE_FORMAT_HVC1:
    case AP4_SAMPLE_FORMAT_DVHE:
    case AP4_SAMPLE_FORMAT_DVH1:
      m_codecHandler = new HEVCCodecHandler(desc);
      break;
    case AP4_SAMPLE_FORMAT_MP4A:
      m_codecHandler = new MPEGCodecHandler(desc);
      break;
    case AP4_SAMPLE_FORMAT_STPP:
      m_codecHandler = new TTMLCodecHandler(desc);
      break;
    case AP4_SAMPLE_FORMAT_WVTT:
      m_codecHandler = new WebVTTCodecHandler(desc, false);
      break;
    case AP4_SAMPLE_FORMAT_VP9:
      m_codecHandler = new VP9CodecHandler(desc);
      break;
    case AP4_SAMPLE_FORMAT_AV01:
      m_codecHandler = new AV1CodecHandler(desc);
      break;
    default:
      m_codecHandler = new CodecHandler(desc);
      break;
  }

  if ((m_decrypterCaps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_ANNEXB_REQUIRED) != 0)
    m_codecHandler->ExtraDataToAnnexB();
}
