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
#include "decrypters/Helpers.h"
#include "decrypters/HelperPr.h"
#include "utils/Bento4Utils.h"
#include "utils/CharArrayParser.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <bento4/Ap4SencAtom.h>

#include <limits>
#include <memory>

using namespace UTILS;

namespace
{
constexpr uint8_t MP4_TFRFBOX_UUID[] = {0xd4, 0x80, 0x7e, 0xf2, 0xca, 0x39, 0x46, 0x95,
                                        0x8e, 0x54, 0x26, 0xcb, 0x9e, 0x46, 0xa7, 0x9f};

constexpr AP4_UI32 FMP4_SAMPLE_FORMAT_WVTT = AP4_ATOM_TYPE('w', 'v', 't', 't');

// Max number of top-level atoms inspected after the MOOV while looking for the first MOOF
// (usually only STYP/SIDX/PRFT/EMSG precede it)
constexpr unsigned int MOOF_SEARCH_MAX_ATOMS = 16;

/*!
 * \brief Read a stream from just after its MOOV up to the first MOOF (first media segment)
 *        and get the PSSH data of a DRM from it. The stream goes back to where it was.
 * \param byteStream The stream, positioned just after the MOOV atom
 * \param systemId The DRM system ID
 * \return The PSSH data (Data field of the PSSH box), empty if not found
 */
std::vector<uint8_t> ExtractMoofPsshData(AP4_ByteStream& byteStream, const uint8_t* systemId)
{
  std::vector<uint8_t> psshData;
  AP4_DefaultAtomFactory atomFactory;
  // Where the reading continues. The position is taken before reading the first atom,
  // when the stream has already moved from the init segment to the first media segment,
  // so that the data of the init segment, already dropped, is not needed
  std::optional<AP4_Position> resumePos;

  for (unsigned int i = 0; i < MOOF_SEARCH_MAX_ATOMS; ++i)
  {
    AP4_Position atomPos{0};
    if (AP4_FAILED(byteStream.Tell(atomPos)))
      break;
    if (!resumePos.has_value())
      resumePos = atomPos;

    AP4_Atom* atom{nullptr};
    if (AP4_FAILED(atomFactory.CreateAtomFromStream(byteStream, atom)) || !atom)
    {
      LOG::LogF(LOGDEBUG, "Cannot read the first media segment");
      break;
    }
    std::unique_ptr<AP4_Atom> atomOwner{atom};

    if (atom->GetType() == AP4_ATOM_TYPE_MDAT)
    {
      LOG::LogF(LOGDEBUG, "No MOOF found in the first media segment");
      break;
    }
    if (atom->GetType() != AP4_ATOM_TYPE_MOOF)
      continue;

    psshData = BENTO4::FindMoofPsshData(AP4_DYNAMIC_CAST(AP4_ContainerAtom, atom), systemId);
    if (psshData.empty())
      LOG::LogF(LOGDEBUG, "No PSSH found in the MOOF of the first media segment");
    break;
  }

  // Go back, the reader starts reading the fragments from there
  if (resumePos.has_value() && AP4_FAILED(byteStream.Seek(*resumePos)))
    LOG::LogF(LOGERROR, "Cannot seek back the stream after reading the first media segment");

  return psshData;
}
} // unnamed namespace


CFragmentedSampleReader::CFragmentedSampleReader(AP4_ByteStream* input,
                                                 AP4_Movie* movie,
                                                 AP4_Track* track)
  : m_track{track}
{
  m_lReader = std::make_shared<CLinearReader>(input, movie);
}

CFragmentedSampleReader::CFragmentedSampleReader(std::shared_ptr<CLinearReader> lReader,
                                                 AP4_Track* track)
  : m_track{track}
{
  m_lReader = lReader;
}

CFragmentedSampleReader::~CFragmentedSampleReader()
{
  // Remove the decryptor pool only with the last CLinearReader instance
  if (m_lReader.use_count() == 1 && m_drmSession)
    m_drmSession->decrypter->RemovePool(m_poolId);

  delete m_codecHandler;
}

bool CFragmentedSampleReader::Initialize(SESSION::CStream* stream)
{
  m_lReader->EnableTrack(m_track->GetId());

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

  // Create codec handler and get extradata
  UpdateSampleDescription();

  return true;
}

std::vector<DRM::DRMInfo> CFragmentedSampleReader::GetInitDRMInfo()
{
  // Read protection info from initialization segment
  std::vector<DRM::DRMInfo> drmInfos;

  std::vector<uint8_t> defaultKid;
  CryptoMode cryptoMode = CryptoMode::NONE;

  AP4_SampleDescription* desc{m_track->GetSampleDescription(0)};
  if (desc->GetType() == AP4_SampleDescription::TYPE_PROTECTED)
  {
    auto protectedDesc = static_cast<AP4_ProtectedSampleDescription*>(desc);

    AP4_UI32 schemeType = protectedDesc->GetSchemeType();
    if (schemeType == AP4_PROTECTION_SCHEME_TYPE_CENC ||
        schemeType == AP4_PROTECTION_SCHEME_TYPE_PIFF)
    {
      cryptoMode = CryptoMode::AES_CTR;
    }
    else if (schemeType == AP4_PROTECTION_SCHEME_TYPE_CBCS)
    {
      cryptoMode = CryptoMode::AES_CBC;
    }
    else if (schemeType == AP4_PROTECTION_SCHEME_TYPE_CBC1 ||
             schemeType == AP4_PROTECTION_SCHEME_TYPE_CENS)
    {
      LOG::LogF(LOGERROR, "Protection scheme %u not implemented.", schemeType);
    }

    AP4_ContainerAtom* schi;
    if (protectedDesc->GetSchemeInfo() && (schi = protectedDesc->GetSchemeInfo()->GetSchiAtom()))
    {
      AP4_TencAtom* tenc(AP4_DYNAMIC_CAST(AP4_TencAtom, schi->GetChild(AP4_ATOM_TYPE_TENC, 0)));
      if (tenc && tenc->GetDefaultKid())
        defaultKid.assign(tenc->GetDefaultKid(), tenc->GetDefaultKid() + 16);
      else
      {
        AP4_PiffTrackEncryptionAtom* piff(AP4_DYNAMIC_CAST(
            AP4_PiffTrackEncryptionAtom, schi->GetChild(AP4_UUID_PIFF_TRACK_ENCRYPTION_ATOM, 0)));
        if (piff && piff->GetDefaultKid())
          defaultKid.assign(piff->GetDefaultKid(), piff->GetDefaultKid() + 16);
      }
    }

    AP4_Movie& movie = m_lReader->GetMovie();

    AP4_Array<AP4_PsshAtom>& pssh{movie.GetPsshAtoms()};

    for (unsigned int i = 0; i < pssh.ItemCount(); ++i)
    {
      AP4_PsshAtom& psshAtom = pssh[i];

      const AP4_DataBuffer& dataBuf = psshAtom.GetData();
      const std::vector<uint8_t> psshData{dataBuf.GetData(),
                                          dataBuf.GetData() + dataBuf.GetDataSize()};

      if (defaultKid.empty() && psshData.empty())
      {
        LOG::LogF(LOGWARNING, "No default KID, and no data from PSSH atom, skipping PSSH");
        continue;
      }
      // Malformed info without default KID, try to extract it from a PSSH
      if (defaultKid.empty() && !psshData.empty())
      {
        LOG::LogF(LOGWARNING, "Unable to get default KID from sample encryption info, trying to "
                              "extract it from PSSH");
        DRM::PSSH psshParser;
        if (psshParser.Parse(DRM::PSSH::Make(psshAtom.GetSystemId(), {}, psshData)))
        {
          const auto& keyIds = psshParser.GetKeyIds();
          if (keyIds.empty())
          {
            LOG::LogF(LOGWARNING, "No KID found in PSSH, skipping PSSH");
            continue;
          }
          if (keyIds.size() > 1)
          {
            LOG::LogF(LOGWARNING, "Multiple KIDs found in PSSH, cannot be determined the default "
                                  "KID, skipping PSSH");
            continue;
          }

          defaultKid = keyIds[0];

          if (defaultKid.empty())
          {
            LOG::LogF(LOGWARNING, "PSSH provided an empty KID, skipping PSSH");
            continue;
          }
        }
      }

      DRM::DRMInfo drmInfo;
      drmInfo.keySystem = DRM::SystemIdToKeySystem(psshAtom.GetSystemId());
      drmInfo.initData = DRM::PSSH::Make(psshAtom.GetSystemId(), {defaultKid}, psshData);
      drmInfo.defaultKid = STRING::ToHexadecimal(defaultKid);
      drmInfo.cryptoMode = cryptoMode;

      // In case of PlayReady, extract the license server URL and encryption type
      if (drmInfo.keySystem == DRM::KS_PLAYREADY)
      {
        DRM::PRHeaderParser protParser;
        if (protParser.Parse(psshData))
        {
          drmInfo.licenseServerUri = protParser.GetLicenseURL();
          auto encryptionType = protParser.GetEncryption();
          if (encryptionType == DRM::PRHeaderParser::EncryptionType::AESCTR)
            drmInfo.cryptoMode = CryptoMode::AES_CTR;
          else if (encryptionType == DRM::PRHeaderParser::EncryptionType::AESCBC)
            drmInfo.cryptoMode = CryptoMode::AES_CBC;
        }
      }

      drmInfos.emplace_back(drmInfo);
    }

    // Some encrypted CMAF streams carry the default KID only in the track's tenc box
    // and do not include a usable PSSH box. Keep the Common Encryption information
    // so a configured decrypter can create its session with that KID.
    if (drmInfos.empty() && !defaultKid.empty())
    {
      DRM::DRMInfo drmInfo;
      drmInfo.defaultKid = STRING::ToHexadecimal(defaultKid);
      drmInfo.cryptoMode = cryptoMode;
      drmInfos.emplace_back(std::move(drmInfo));
      LOG::LogF(LOGDEBUG, "Found protected sample description KID without usable PSSH");
    }
  }

  return drmInfos;
}

std::vector<uint8_t> CFragmentedSampleReader::GetMediaSegmentPsshData(const uint8_t* systemId)
{
  AP4_ByteStream* byteStream = m_lReader->GetByteStream();
  if (!byteStream)
    return {};

  return ExtractMoofPsshData(*byteStream, systemId);
}

void CFragmentedSampleReader::SetDecrypter(std::shared_ptr<DRM::DRMSession> drmSession)
{
  if (drmSession)
  {
    m_poolId = drmSession->decrypter->AddPool();
    m_drmSession = drmSession;
  }
}

AP4_Result CFragmentedSampleReader::Start(std::optional<uint64_t> pts)
{
  if (m_started)
    return AP4_SUCCESS;

  AP4_Result ret;
  if (pts.has_value())
    TimeSeek(*pts) ? ret = AP4_SUCCESS : ret = AP4_ERROR_EOS;
  else
    ret = ReadSample();

  m_started = AP4_SUCCEEDED(ret);
  return ret;
}

AP4_Result CFragmentedSampleReader::ReadSample()
{
  if (!m_codecHandler)
    return AP4_FAILURE;

  DRM::DRMMediaType streamType = DRM::DRMMediaType::VIDEO;
  if (m_track->GetType() == AP4_Track::Type::TYPE_AUDIO)
    streamType = DRM::DRMMediaType::AUDIO;

  AP4_Result result;
  AP4_DataBuffer sampleData;
  if (!m_codecHandler->ReadNextSample(m_sample, m_sampleData))
  {
    if (AP4_FAILED(result = m_lReader->ExecReadNextSample(this, m_track->GetId(), m_sample, sampleData)))
    {
      m_sampleData.SetDataSize(0);

      if (result == AP4_ERROR_EOS)
      {
        auto adByteStream = dynamic_cast<CAdaptiveByteStream*>(m_lReader->GetByteStream());
        if (!adByteStream)
        {
          LOG::LogF(LOGERROR, "Fragment stream cannot be casted to AdaptiveByteStream");
          m_eos = true;
        }
        else
        {
          if (adByteStream->waitingForSegment())
          {
            // if waiting for segment, we will return success to avoid EOS and let the caller to call ReadSample again
            result = AP4_SUCCESS;
          }
          else
            m_eos = true;
        }
      }
      return result;
    }

    //AP4_AvcSequenceParameterSet sps;
    //AP4_AvcFrameParser::ParseFrameForSPS(m_sampleData.GetData(), m_sampleData.GetDataSize(), 4, sps);

    //Protection could have changed in ProcessMoof
    bool useDecryptingDecoder =
        m_drmSession && (m_drmSession->capabilities.HasFlag(DRM::Capabilities::SECURE_PATH));

    if (m_decrypter)
    {
      m_sampleData.Reserve(sampleData.GetDataSize());
      if (AP4_FAILED(result = m_decrypter->DecryptSampleData(m_poolId, sampleData, m_sampleData,
                                                        NULL, streamType)))
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
      // Should fall here for the specific case of unencrypted content,
      // to help convert packets to annex-b
      // it depends on decrypter implementation
      //! @todo: it appears to have been implemented in the past for Widevine
      //!        and may not be suitable for other decryptors
      m_sampleData.Reserve(sampleData.GetDataSize());
      if (AP4_FAILED(
              result = m_drmSession->decrypter->DecryptSampleData(
                  m_poolId, sampleData, m_sampleData, nullptr, 0, nullptr, nullptr, streamType)))
      {
        Reset(true);
        return result;
      }
    }
    else
    {
      m_sampleData.SetData(sampleData.GetData(), sampleData.GetDataSize());
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
  m_lReader->Reset(); // Note: all shared readers call the same shared reader reset

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
  return m_decrypter && m_drmSession->capabilities.HasFlag(DRM::Capabilities::SECURE_PATH);
}

bool CFragmentedSampleReader::GetInformation(kodi::addon::InputstreamInfo& info)
{
  if (!m_codecHandler)
    return false;

  // Note: when a manifest provides extradata, it will already be set to InputstreamInfo

  bool isChanged{false};
  if (m_bSampleDescChanged && !m_codecHandler->m_extraData.empty() &&
      !info.CompareExtraData(m_codecHandler->m_extraData.data(),
                             m_codecHandler->m_extraData.size()))
  {
    info.SetExtraData(m_codecHandler->m_extraData);
    isChanged = true;
  }

  std::vector<uint8_t> extraData = info.GetExtraData();
  if (m_codecHandler->CheckExtraData(
          extraData,
          (m_drmSession && m_drmSession->capabilities.HasFlag(DRM::Capabilities::ANNEXB_REQUIRED))))
  {
    m_codecHandler->m_extraData = extraData;
    info.SetExtraData(extraData);
    isChanged = true;
  }

  isChanged |= m_codecHandler->GetInformation(info);

  m_bSampleDescChanged = false;

  return isChanged;
}

bool CFragmentedSampleReader::TimeSeek(uint64_t pts)
{
  // compensate the pts diff with the manifest timing
  pts += m_ptsDiff;

  AP4_Ordinal sampleIndex;
  AP4_UI64 seekPos(static_cast<AP4_UI64>((pts * m_timeBaseInt) / m_timeBaseExt));

  AP4_Result result = m_lReader->SeekSample(this, m_track->GetId(), seekPos, sampleIndex);
  if (AP4_FAILED(result))
  {
    if (result != AP4_ERROR_EOS)
      LOG::LogF(LOGERROR, "Cannot seek track id %u, error %i", m_track->GetId(), result);

    return false;
  }

  if (m_decrypter)
    m_decrypter->SetSampleIndex(sampleIndex);

  if (m_codecHandler)
    m_codecHandler->TimeSeek(seekPos);

  m_started = true;
  return AP4_SUCCEEDED(ReadSample());
}

void CFragmentedSampleReader::SetPTSOffset(uint64_t offset)
{
  m_lReader->FindTracker(m_track->GetId())->m_NextDts = (offset * m_timeBaseInt) / m_timeBaseExt;
  m_ptsOffs = offset;

  if (m_codecHandler)
    m_codecHandler->SetPTSOffset((offset * m_timeBaseInt) / m_timeBaseExt);
}

bool CFragmentedSampleReader::GetFragmentInfo(uint64_t& duration)
{
  auto fragSampleTable = dynamic_cast<AP4_FragmentSampleTable*>(
      m_lReader->FindTracker(m_track->GetId())->m_SampleTable);
  if (fragSampleTable)
    duration = fragSampleTable->GetDuration();
  else
  {
    LOG::LogF(LOGERROR, "Can't get FragmentSampleTable from track %u", m_track->GetId());
    return false;
  }
  return true;
}

std::unique_ptr<ISampleReader> CFragmentedSampleReader::CreateReaderByTrack()
{
  auto& movie = m_lReader->GetMovie();

  // Note that CLinearReader (AP4_LinearReader) will be shared among all readers created,
  // which read from a different track ID
  AP4_Track* selTrack = movie.GetTrack(AP4_Track::Type::TYPE_AUDIO, 0);
  if (!selTrack)
  {
    LOG::LogF(LOGERROR, "Audio track not found");
    return nullptr;
  }

  auto newFragReader = std::make_unique<CFragmentedSampleReader>(m_lReader, selTrack);
  newFragReader->SetDecrypter(m_drmSession);

  LOG::LogF(LOGDEBUG, "Created shared reader for audio track id %u", selTrack->GetId());

  return std::move(newFragReader);
}

AP4_Result CFragmentedSampleReader::ProcessMoof(AP4_ContainerAtom* moof,
                                                AP4_Position moof_offset,
                                                AP4_Position mdat_payload_offset,
                                                AP4_UI64 mdat_payload_size)
{
  m_fragmentKid.clear();

  // Clone outside AP4_DYNAMIC_CAST: with AP4_CONFIG_NO_RTTI (set for Android in Ap4Config.h)
  // the macro evaluates its argument twice, which cloned the moof twice and leaked one copy
  // (including its trun and senc atoms) for every fragment.
  AP4_Atom* moofClone = moof->Clone();
  AP4_MovieFragment fragment = AP4_MovieFragment(AP4_DYNAMIC_CAST(AP4_ContainerAtom, moofClone));
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

  if (AP4_SUCCEEDED((result = m_lReader->ExecProcessMoof(moof, moof_offset, mdat_payload_offset,
                                                         mdat_payload_size))))
  {
    AP4_ContainerAtom* traf =
        AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof->GetChild(AP4_ATOM_TYPE_TRAF, 0));

    if (!traf)
    {
      LOG::LogF(LOGERROR, "Cannot get TRAF atom");
      return AP4_ERROR_NO_SUCH_ITEM;
    }

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

      std::vector<uint8_t> extradata = m_codecHandler->m_extraData;
      if (m_codecHandler->CheckExtraData(
              extradata, (m_drmSession &&
                          m_drmSession->capabilities.HasFlag(DRM::Capabilities::ANNEXB_REQUIRED))))
      {
        m_codecHandler->m_extraData = extradata;
      }
    }

    //Correct PTS
    AP4_Sample sample;
    //! @todo: there is something wrong on pts calculation,
    //! m_ptsOffs have a value in seconds and so the substraction "m_pts - m_ptsOffs" looks to be inconsistent.
    //! This code is present also on the others sample readers, that need to be verified
    if (~m_ptsOffs)
    {
      if (AP4_SUCCEEDED(m_lReader->GetSample(m_track->GetId(), sample, 0)))
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

      AP4_ContainerAtom* traf =
          AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof->GetChild(AP4_ATOM_TYPE_TRAF, 0));

      if (!traf)
        return AP4_ERROR_INVALID_FORMAT;

      ParseMoofPssh(moof);
      ParseTrafSeig(traf);

      bool isDefaultProtected{true};
      AP4_ContainerAtom* schi = m_protectedDesc->GetSchemeInfo()->GetSchiAtom();
      if (schi)
      {
        AP4_CencTrackEncryption* trackEnc =
            AP4_DYNAMIC_CAST(AP4_CencTrackEncryption, schi->GetChild(AP4_ATOM_TYPE_TENC));
        if (!trackEnc)
          trackEnc = AP4_DYNAMIC_CAST(AP4_CencTrackEncryption,
                                      schi->GetChild(AP4_UUID_PIFF_TRACK_ENCRYPTION_ATOM));
        if (trackEnc)
          isDefaultProtected = trackEnc->GetDefaultIsProtected() != 0;
      }

      // If the boxes saiz, saio, senc are missing, the stream does not conform to the specs and
      // may not be decrypted, so try create an empty senc where all samples will use the same default IV
      if (isDefaultProtected && !traf->GetChild(AP4_ATOM_TYPE_SAIO) &&
          !traf->GetChild(AP4_ATOM_TYPE_SAIZ) && !traf->GetChild(AP4_ATOM_TYPE_SENC) &&
          !traf->GetChild(AP4_UUID_PIFF_SAMPLE_ENCRYPTION_ATOM))
      {
        traf->AddChild(new AP4_SencAtom());
      }

      bool reset_iv(false);
      if (AP4_FAILED(result = AP4_CencSampleInfoTable::Create(m_protectedDesc, traf, algorithm_id,
                                                              reset_iv, *m_lReader->GetByteStream(),
                                                              moof_offset, sample_table)))
        // we assume unencrypted fragment here
        goto SUCCESS;

      if (!m_drmSession)
        return AP4_ERROR_INVALID_PARAMETERS;

      m_decrypter = std::make_unique<CAdaptiveCencSampleDecrypter>(m_drmSession->decrypter, sample_table);

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
    else
    {
      // Reset for unencrypted content
      m_decrypter.reset();
      m_readerCryptoInfo = CryptoInfo();
    }
  }
SUCCESS:
  if (m_drmSession && m_codecHandler)
  {
    // The KID of the SEIG sample group has precedence over the default KID (key rotation)
    const std::vector<uint8_t> kid =
        m_fragmentKid.empty() ? DRM::ConvertKidStrToBytes(m_drmSession->kid) : m_fragmentKid;

    if (AP4_FAILED(m_drmSession->decrypter->SetFragmentInfo(
            m_poolId, kid, m_codecHandler->m_naluLengthSize, m_codecHandler->m_extraData,
            m_drmSession->capabilities, m_readerCryptoInfo)))
    {
      return AP4_ERROR_INVALID_FORMAT;
    }
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
  else {
    m_protectedDesc = nullptr;
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
        m_codecHandler = new HEVCCodecHandler(desc, m_track);
        break;
      case AP4_SAMPLE_FORMAT_STPP:
        m_codecHandler = new TTMLCodecHandler(desc, false);
        break;
      case FMP4_SAMPLE_FORMAT_WVTT:
        m_codecHandler = new WebVTTCodecHandler(desc, false);
        break;
      case AP4_SAMPLE_FORMAT_VP9:
        m_codecHandler = new VP9CodecHandler(desc);
        break;
      case AP4_SAMPLE_FORMAT_AV01:
        m_codecHandler = new AV1CodecHandler(desc, m_track);
        break;
      default:
        m_codecHandler = new CodecHandler(desc);
        break;
    }
  }
}

void CFragmentedSampleReader::ParseTrafTfrf(AP4_UuidAtom* uuidAtom)
{
  auto* unknownUuidAtom = dynamic_cast<AP4_UnknownUuidAtom*>(uuidAtom);
  if (!unknownUuidAtom)
  {
    LOG::LogF(LOGERROR, "Invalid atom type passed to ParseTrafTfrf. Expected AP4_UnknownUuidAtom.");
    return;
  }

  auto* accessor = reinterpret_cast<BENTO4::FMP4UnknownUuidAtom*>(unknownUuidAtom);
  const AP4_DataBuffer& buf{accessor->GetData()};
  CCharArrayParser parser;
  parser.Reset(buf.GetData(), buf.GetDataSize());

  if (parser.CharsLeft() < 5)
  {
    LOG::LogF(LOGERROR, "Wrong data length on TFRF atom.");
    return;
  }
  uint8_t version = parser.ReadNextUnsignedChar();
  [[maybe_unused]] uint32_t flags = parser.ReadNextUnsignedInt24();
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

void CFragmentedSampleReader::ParseTrafSeig(AP4_ContainerAtom* traf)
{
  bool isPartial{false};
  std::vector<uint8_t> kid = BENTO4::GetTrafSeigKid(traf, isPartial);

  // A KID of zeros is a placeholder, the default KID applies
  if (DRM::IsAllZeroKey(kid))
    kid.clear();

  if (isPartial && !m_isSeigPartialWarned)
  {
    m_isSeigPartialWarned = true;
    LOG::LogF(LOGWARNING, "The samples of a fragment use multiple KIDs (SEIG sample group), "
                          "this is not supported, only the first KID is used");
  }

  if (!kid.empty() && kid != m_lastSeigKid)
  {
    LOG::LogF(LOGDEBUG, "Fragment KID from the SEIG sample group: %s",
              STRING::ToHexadecimal(kid).c_str());
    m_lastSeigKid = kid;
  }
  m_fragmentKid = std::move(kid);
}

void CFragmentedSampleReader::ParseMoofPssh(AP4_ContainerAtom* moof)
{
  std::vector<AP4_PsshAtom*> psshEntries;

  AP4_Atom* childAtom{nullptr};
  unsigned int atomIndex{0};

  while ((childAtom = moof->GetChild(AP4_ATOM_TYPE_PSSH, atomIndex++)) != nullptr)
  {
    auto psshAtom = static_cast<AP4_PsshAtom*>(childAtom);
    psshEntries.emplace_back(psshAtom);
  }

  if (!psshEntries.empty())
  {
    //! @todo: Media segments can contains PSSH also when there is no Key rotation
    //! @todo: Key rotation feature not implemented
    //! Possible use case: MPD dont provide KID/PSSH and init segment has default KID 00000000000000000000000000000000
    //! a placeholder that means no default KID, so its needed initialize DRM later time when we can
    //! parse a media segment with a PSSH (or SEIG box)
    LOG::LogF(LOGDEBUG, "Found %zu PSSH on media segment", psshEntries.size());
  }
}

CLinearReader::CLinearReader(AP4_ByteStream* input, AP4_Movie* movie)
  : AP4_LinearReader{*movie, input}
{
}

void CLinearReader::SetCallback(CLinearReaderCB* lReaderCB)
{
  m_lReaderCB = lReaderCB;
}

AP4_Result CLinearReader::ExecReadNextSample(CLinearReaderCB* lReaderCB,
                                             AP4_UI32 track_id,
                                             AP4_Sample& sample,
                                             AP4_DataBuffer& sample_data)
{
  // Protect call to ReadNextSample until method exit,
  // to ensure appropriate callback to ProcessMoof
  // with the specified reader (CLinearReaderCB) instance
  std::lock_guard<std::mutex> lock(m_readerMtx);
  SetCallback(lReaderCB);
  return AP4_LinearReader::ReadNextSample(track_id, sample, sample_data);
}

AP4_Result CLinearReader::ExecProcessMoof(AP4_ContainerAtom* moof,
                                          AP4_Position moof_offset,
                                          AP4_Position mdat_payload_offset,
                                          AP4_UI64 mdat_payload_size)
{
  return AP4_LinearReader::ProcessMoof(moof, moof_offset, mdat_payload_offset, mdat_payload_size);
}

AP4_LinearReader::Tracker* CLinearReader::FindTracker(AP4_UI32 track_id)
{
  return AP4_LinearReader::FindTracker(track_id);
}

AP4_Result CLinearReader::GetSample(AP4_UI32 track_id, AP4_Sample& sample, AP4_Ordinal sample_index)
{
  // look for a sample from a specific track
  Tracker* tracker = FindTracker(track_id);
  if (tracker == NULL)
    return AP4_ERROR_INVALID_PARAMETERS;

  // don't continue if we've reached the end of that tracker
  if (tracker->m_Eos)
    return AP4_ERROR_EOS;

  return tracker->m_SampleTable->GetSample(sample_index, sample);
}

void CLinearReader::Reset()
{
  std::lock_guard<std::mutex> lock(m_readerMtx);

  // flush any queued samples
  FlushQueues();

  // reset tracker states
  for (unsigned int i = 0; i < m_Trackers.ItemCount(); i++)
  {
    if (m_Trackers[i]->m_SampleTableIsOwned)
    {
      delete m_Trackers[i]->m_SampleTable;
    }
    delete m_Trackers[i]->m_NextSample;
    m_Trackers[i]->m_SampleTable = NULL;
    m_Trackers[i]->m_NextSample = NULL;
    m_Trackers[i]->m_NextSampleIndex = 0;
    m_Trackers[i]->m_Eos = false;
  }
  m_NextFragmentPosition = 0;
}

AP4_ByteStream* CLinearReader::GetByteStream()
{
  return AP4_LinearReader::m_FragmentStream;
}

AP4_Movie& CLinearReader::GetMovie()
{
  return AP4_LinearReader::m_Movie;
}

AP4_Result CLinearReader::SeekSample(CLinearReaderCB* lReaderCB,
                                     AP4_UI32 track_id,
                                     AP4_UI64 ts,
                                     AP4_Ordinal& sample_index)
{
  // we only support fragmented sources for now
  if (!m_HasFragments)
    return AP4_ERROR_NOT_SUPPORTED;

  if (m_Trackers.ItemCount() == 0)
  {
    return AP4_ERROR_NO_SUCH_ITEM;
  }

  // Protect call to SeekSample until method exit,
  // to ensure appropriate callback to ProcessMoof from Advance
  // with the specified reader (CLinearReaderCB) instance
  std::lock_guard<std::mutex> lock(m_readerMtx);
  SetCallback(lReaderCB);

  // look for a sample from a specific track
  Tracker* tracker = FindTracker(track_id);
  if (tracker == NULL)
    return AP4_ERROR_INVALID_PARAMETERS;

  // don't continue if we've reached the end of that tracker
  if (tracker->m_Eos)
    return AP4_ERROR_EOS;

  AP4_Result result;

  if (!tracker->m_SampleTable)
  {
    if (AP4_FAILED(result = Advance()))
      return result;
    // Ensure that Advance has populated the sample table
    if (!tracker->m_SampleTable)
    {
      LOG::LogF(LOGERROR, "No sample table");
      return AP4_ERROR_INVALID_STATE;
    }
  }

  //! @todo: remove our custom GetNearestSyncSampleIndex from Bento4
  AP4_Cardinal samplesCount = tracker->m_SampleTable->GetSampleCount();

  // No samples, attempt advance to (download/read) next segment
  if (samplesCount == 0)
  {
    tracker->m_NextSampleIndex = tracker->m_SampleTable->GetSampleCount();

    const bool isOwned = tracker->m_SampleTableIsOwned;
    // Temporarily set m_SampleTableIsOwned to false to prevent "Advance" method
    // from deleting the tracker sample table
    tracker->m_SampleTableIsOwned = false;

    if (AP4_FAILED(result = Advance()))
    {
      tracker->m_SampleTableIsOwned = isOwned;
      return result;
    }
    // Ensure that Advance has populated the sample table
    if (!tracker->m_SampleTable)
    {
      LOG::LogF(LOGERROR, "No sample table");
      return AP4_ERROR_INVALID_STATE;
    }

    tracker->m_SampleTableIsOwned = isOwned;
    samplesCount = tracker->m_SampleTable->GetSampleCount();

    if (samplesCount == 0)
    {
      LOG::LogF(LOGERROR, "No samples found in the segment packet");
      return AP4_ERROR_INVALID_STATE;
    }
  }

  constexpr AP4_Cardinal indexNoValue{std::numeric_limits<AP4_Cardinal>::max()};
  AP4_Cardinal syncIndex{indexNoValue};
  AP4_UI64 syncCts{0};
  AP4_Cardinal sampleIndex{indexNoValue};

  // Try find a sample "sync", when possible, with equal or higher ts than the target ts
  // or, fallback to the sample (without sync) with nearest ts
  // NOTE: search exclusively within this segment,
  // avoiding requesting (download/read) other segments to speedup the seek operation
  for (AP4_Cardinal index{0}; index < samplesCount; ++index)
  {
    AP4_Sample sample;
    if (AP4_FAILED(tracker->m_SampleTable->GetSample(index, sample)))
      continue;

    const AP4_UI64 cts = sample.GetCts();
    if (sample.IsSync())
    {
      if (syncIndex == indexNoValue || syncCts < ts && cts > syncCts)
      {
        syncCts = cts;
        syncIndex = index;
      }
    }
    // Find any sample (no sync) closer to the target ts
    if (cts <= ts)
      sampleIndex = index;
  }

  if (syncIndex != indexNoValue)
    sample_index = syncIndex;
  else if (sampleIndex != indexNoValue)
  {
    LOG::LogF(LOGDEBUG, "No sample sync found, fallback to sample with nearest timestamp");
    sample_index = sampleIndex;
  }
  else
  {
    LOG::LogF(LOGERROR, "Cannot determine the sample index");
    return AP4_ERROR_INVALID_STATE;
  }

  if (!tracker->m_SampleTable) // Required for SetSampleIndex
  {
    LOG::LogF(LOGERROR, "Missing sample table, cannot set the sample index");
    return AP4_ERROR_INVALID_STATE;
  }

  return SetSampleIndex(tracker->m_Track->GetId(), sample_index);
}

AP4_Result CLinearReader::ProcessMoof(AP4_ContainerAtom* moof,
                                      AP4_Position moof_offset,
                                      AP4_Position mdat_payload_offset,
                                      AP4_UI64 mdat_payload_size)
{
  if (!m_lReaderCB)
  {
    LOG::LogF(LOGFATAL, "Unable to execute ProcessMoof, CLinearReaderCB is not set");
    return AP4_ERROR_INVALID_STATE;
  }
  return m_lReaderCB->ProcessMoof(moof, moof_offset, mdat_payload_offset, mdat_payload_size);
}
