/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "FragmentedSampleReader.h"

#include "AdaptiveByteStream.h"
#include "codechandler/AV1CodecHandler.h"
#include "codechandler/AVCCodecHandler.h"
#include "codechandler/AudioCodecHandler.h"
#include "codechandler/CodecHandler.h"
#include "codechandler/HEVCCodecHandler.h"
#include "codechandler/TTMLCodecHandler.h"
#include "codechandler/VP9CodecHandler.h"
#include "codechandler/WebVTTCodecHandler.h"
#include "common/AdaptiveCencSampleDecrypter.h"
#include "utils/CharArrayParser.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <bento4/Ap4SencAtom.h>

using namespace UTILS;

namespace
{
constexpr uint8_t MP4_TFRFBOX_UUID[] = {0xd4, 0x80, 0x7e, 0xf2, 0xca, 0x39, 0x46, 0x95,
                                        0x8e, 0x54, 0x26, 0xcb, 0x9e, 0x46, 0xa7, 0x9f};
} // unnamed namespace


CFragmentedSampleReader::CFragmentedSampleReader(AP4_ByteStream* input,
                                                 AP4_Movie* movie,
                                                 AP4_Track* track,
                                                 AP4_UI32 streamId)
  : AP4_LinearReader{*movie, input},
    m_track{track},
    m_streamId{streamId}
{
}

CFragmentedSampleReader::~CFragmentedSampleReader()
{
  if (m_singleSampleDecryptor)
    m_singleSampleDecryptor->RemovePool(m_poolId);
  delete m_decrypter;
  delete m_codecHandler;
}

bool CFragmentedSampleReader::Initialize(SESSION::CStream* stream)
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
      if (tenc && tenc->GetDefaultKid())
        m_defaultKey.assign(tenc->GetDefaultKid(), tenc->GetDefaultKid() + 16);
      else
      {
        AP4_PiffTrackEncryptionAtom* piff(AP4_DYNAMIC_CAST(
            AP4_PiffTrackEncryptionAtom, schi->GetChild(AP4_UUID_PIFF_TRACK_ENCRYPTION_ATOM, 0)));
        if (piff && piff->GetDefaultKid())
          m_defaultKey.assign(piff->GetDefaultKid(), piff->GetDefaultKid() + 16);
      }
    }
  }

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

  return true;
}

void CFragmentedSampleReader::SetDecrypter(Adaptive_CencSingleSampleDecrypter* ssd,
                                           const DRM::DecrypterCapabilites& dcaps)
{
  if (ssd)
  {
    m_poolId = ssd->AddPool();
    m_singleSampleDecryptor = ssd;
  }
  
  m_decrypterCaps = dcaps;

  // We need this to fill extradata
  UpdateSampleDescription();
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
  if (!m_codecHandler)
    return AP4_FAILURE;

  AP4_Result result;
  if (!m_codecHandler->ReadNextSample(m_sample, m_sampleData))
  {
    bool useDecryptingDecoder =
        m_protectedDesc &&
        (m_decrypterCaps.flags & DRM::DecrypterCapabilites::SSD_SECURE_PATH) != 0;
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
      m_sampleData.Reserve(m_encrypted.GetDataSize());
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
      m_sampleData.Reserve(m_encrypted.GetDataSize());
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
  return (m_decrypterCaps.flags & DRM::DecrypterCapabilites::SSD_SECURE_PATH) != 0 &&
         m_decrypter != nullptr;
}

bool CFragmentedSampleReader::GetInformation(kodi::addon::InputstreamInfo& info)
{
  if (!m_codecHandler)
    return false;

  bool isChanged{false};
  if (m_bSampleDescChanged && m_codecHandler->m_extraData.GetDataSize() &&
      !info.CompareExtraData(m_codecHandler->m_extraData.GetData(),
                             m_codecHandler->m_extraData.GetDataSize()))
  {
    info.SetExtraData(m_codecHandler->m_extraData.GetData(),
                      m_codecHandler->m_extraData.GetDataSize());
    isChanged |= true;
  }

  m_bSampleDescChanged = false;

  isChanged |= m_codecHandler->GetInformation(info);

  return isChanged;
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

bool CFragmentedSampleReader::GetFragmentInfo(uint64_t& duration)
{
  auto fragSampleTable =
      dynamic_cast<AP4_FragmentSampleTable*>(FindTracker(m_track->GetId())->m_SampleTable);
  if (fragSampleTable)
    duration = fragSampleTable->GetDuration();
  else
  {
    LOG::LogF(LOGERROR, "Can't get FragmentSampleTable from track %u", m_track->GetId());
    return false;
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
    if (m_track->GetId() == AP4_TRACK_ID_UNKNOWN)
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

    AP4_Atom* atom{nullptr};
    unsigned int atom_pos{0};

    while ((atom = traf->GetChild(AP4_ATOM_TYPE_UUID, atom_pos++)) != nullptr)
    {
      AP4_UuidAtom* uuidAtom{AP4_DYNAMIC_CAST(AP4_UuidAtom, atom)};
      // Some Dash and Smooth Streaming live streaming can be segment controlled
      // these type of manifests dont have scheduled manifest updates
      // so its needed to parse the TFRF in order to get new updates
      if (std::memcmp(uuidAtom->GetUuid(), MP4_TFRFBOX_UUID, 16) == 0)
      {
        ParseTrafTfrf(uuidAtom);
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
    //! @todo: there is something wrong on pts calculation,
    //! m_ptsOffs have a value in seconds and so the substraction "m_pts - m_ptsOffs" looks to be inconsistent.
    //! This code is present also on the others sample readers, that need to be verified
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

      // If the boxes saiz, saio, senc are missing, the stream does not conform to the specs and
      // may not be decrypted, so try create an empty senc where all samples will use the same default IV
      if (!traf->GetChild(AP4_ATOM_TYPE_SAIO) && !traf->GetChild(AP4_ATOM_TYPE_SAIZ) &&
          !traf->GetChild(AP4_ATOM_TYPE_SENC))
      {
        traf->AddChild(new AP4_SencAtom());
      }

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
  {
    delete m_codecHandler;
    m_codecHandler = nullptr;
  }
  m_bSampleDescChanged = true;

  AP4_SampleDescription* desc = m_track->GetSampleDescription(m_sampleDescIndex - 1);
  if (!desc)
  {
    LOG::LogF(LOGERROR, "Cannot get sample description from index %u", m_sampleDescIndex - 1);
    return;
  }

  if (desc->GetType() == AP4_SampleDescription::TYPE_PROTECTED)
  {
    m_protectedDesc = static_cast<AP4_ProtectedSampleDescription*>(desc);
    desc = m_protectedDesc->GetOriginalSampleDescription();
    if (!desc)
    {
      LOG::LogF(LOGERROR, "Cannot sample description from protected sample description");
      return;
    }
  }

  LOG::LogF(LOGDEBUG, "Codec fourcc: %s (%u)", CODEC::FourCCToString(desc->GetFormat()).c_str(),
            desc->GetFormat());
  
  if (AP4_DYNAMIC_CAST(AP4_AudioSampleDescription, desc))
  {
    // Audio sample of any format
    m_codecHandler = new AudioCodecHandler(desc);
  }
  else
  {
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
      case AP4_SAMPLE_FORMAT_STPP:
        m_codecHandler = new TTMLCodecHandler(desc, false);
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
  }

  if ((m_decrypterCaps.flags & DRM::DecrypterCapabilites::SSD_ANNEXB_REQUIRED) != 0)
    m_codecHandler->ExtraDataToAnnexB();
}

void CFragmentedSampleReader::ParseTrafTfrf(AP4_UuidAtom* uuidAtom)
{
  const AP4_DataBuffer& buf{AP4_DYNAMIC_CAST(AP4_UnknownUuidAtom, uuidAtom)->GetData()};
  CCharArrayParser parser;
  parser.Reset(buf.GetData(), buf.GetDataSize());

  if (parser.CharsLeft() < 5)
  {
    LOG::LogF(LOGERROR, "Wrong data length on TFRF atom.");
    return;
  }
  uint8_t version = parser.ReadNextUnsignedChar();
  uint32_t flags = parser.ReadNextUnsignedInt24();
  uint8_t fragmentCount = parser.ReadNextUnsignedChar();

  for (uint8_t index = 0; index < fragmentCount; index++)
  {
    uint64_t time;
    uint64_t duration;

    if (version == 0)
    {
      time = static_cast<uint64_t>(parser.ReadNextUnsignedInt());
      duration = static_cast<uint64_t>(parser.ReadNextUnsignedInt());
    }
    else if (version == 1)
    {
      time = parser.ReadNextUnsignedInt64();
      duration = parser.ReadNextUnsignedInt64();
    }
    else
    {
      LOG::LogF(LOGWARNING, "Version %u of TFRF atom fragment is not supported.", version);
      return;
    }
    m_observer->OnTFRFatom(time, duration, m_track->GetMediaTimeScale());
  }
}
