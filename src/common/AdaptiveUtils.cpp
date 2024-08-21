/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AdaptiveUtils.h"

#include "AdaptiveStream.h"
#include "Representation.h"
#include "decrypters/Helpers.h"
#include "utils/log.h"
#include "utils/Utils.h"

#include <cinttypes>
#include <cstdio> // sscanf

#include <bento4/Ap4.h>

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

using namespace UTILS;

std::string_view PLAYLIST::StreamTypeToString(const StreamType streamType)
{
  switch (streamType)
  {
    case StreamType::VIDEO:
      return "video";
    case StreamType::AUDIO:
      return "audio";
    case StreamType::SUBTITLE:
      return "subtitle";
    case StreamType::VIDEO_AUDIO:
      return "video-audio";
    default:
      return "unknown";
  }
}

bool PLAYLIST::ParseRangeRFC(std::string_view range, uint64_t& start, uint64_t& end)
{
  //! @todo: must be reworked as https://httpwg.org/specs/rfc7233.html
  uint64_t startVal{0};
  uint64_t endVal{0};
  if (std::sscanf(range.data(), "%" SCNu64 "-%" SCNu64, &startVal, &endVal) > 0)
  {
    start = startVal;
    end = endVal;
    return true;
  }
  return false;
}

bool PLAYLIST::ParseRangeValues(std::string_view range,
                                uint64_t& first,
                                uint64_t& second,
                                char separator /* = '@' */)
{
  std::string pattern = "%llu";
  pattern.push_back(separator);
  pattern.append("%llu");

  if (std::sscanf(range.data(), pattern.c_str(), &first, &second) > 0)
    return true;

  return false;
}

AP4_Movie* PLAYLIST::CreateMovieAtom(adaptive::AdaptiveStream& adStream,
                                     kodi::addon::InputstreamInfo& streamInfo)
{
  CRepresentation* repr = adStream.getRepresentation();
  const std::vector<uint8_t>& extradata = repr->GetCodecPrivateData();
  const std::string codecName = streamInfo.GetCodecName();
  AP4_SampleDescription* sampleDesc;

  if (codecName == CODEC::NAME_H264)
  {
    AP4_MemoryByteStream ms{extradata.data(), static_cast<const AP4_Size>(extradata.size())};
    AP4_AvccAtom* atom =
        AP4_AvccAtom::Create(static_cast<AP4_Size>(AP4_ATOM_HEADER_SIZE + extradata.size()), ms);
    sampleDesc = new AP4_AvcSampleDescription(AP4_SAMPLE_FORMAT_AVC1, streamInfo.GetWidth(),
                                              streamInfo.GetHeight(), 0, nullptr, atom);
  }
  else if (codecName == CODEC::NAME_HEVC)
  {
    AP4_MemoryByteStream ms{extradata.data(), static_cast<const AP4_Size>(extradata.size())};
    AP4_HvccAtom* atom =
        AP4_HvccAtom::Create(static_cast<AP4_Size>(AP4_ATOM_HEADER_SIZE + extradata.size()), ms);
    sampleDesc = new AP4_HevcSampleDescription(AP4_SAMPLE_FORMAT_HEV1, streamInfo.GetWidth(),
                                               streamInfo.GetHeight(), 0, nullptr, atom);
  }
  else if (codecName == CODEC::NAME_AV1)
  {
    AP4_MemoryByteStream ms{extradata.data(), static_cast<const AP4_Size>(extradata.size())};
    AP4_Av1cAtom* atom =
        AP4_Av1cAtom::Create(static_cast<AP4_Size>(AP4_ATOM_HEADER_SIZE + extradata.size()), ms);
    sampleDesc = new AP4_Av1SampleDescription(AP4_SAMPLE_FORMAT_AV01, streamInfo.GetWidth(),
                                              streamInfo.GetHeight(), 0, nullptr, atom);
  }
  else if (codecName == CODEC::NAME_SRT)
  {
    sampleDesc =
        new AP4_SampleDescription(AP4_SampleDescription::TYPE_SUBTITLES, AP4_SAMPLE_FORMAT_STPP, 0);
  }
  else
  {
    // Codecs like audio types, will have unknown SampleDescription, because to create an appropriate
    // audio SampleDescription atom require different code rework. This means also that CFragmentedSampleReader
    // will use a generic CodecHandler instead of AudioCodecHandler, because will be not able do determine the codec
    LOG::LogF(LOGDEBUG,
              "Created sample description atom of unknown type for codec \"%s\" because unhandled",
              codecName.data());
    sampleDesc = new AP4_SampleDescription(AP4_SampleDescription::TYPE_UNKNOWN, 0, 0);
  }

  if (repr->GetPsshSetPos() != PSSHSET_POS_DEFAULT)
  {
    const PLAYLIST::CPeriod::PSSHSet& psshSet =
        adStream.getPeriod()->GetPSSHSets()[repr->GetPsshSetPos()];

    std::vector<uint8_t> defaultKid;
    if (psshSet.defaultKID_.empty())
      defaultKid.assign(DEFAULT_KEYID, DEFAULT_KEYID + 16);
    else
      defaultKid = DRM::ConvertKidStrToBytes(psshSet.defaultKID_);

    AP4_ContainerAtom schi{AP4_ATOM_TYPE_SCHI};
    schi.AddChild(new AP4_TencAtom(AP4_CENC_CIPHER_AES_128_CTR, 8, defaultKid.data()));
    sampleDesc = new AP4_ProtectedSampleDescription(0, sampleDesc, 0,
                                                    AP4_PROTECTION_SCHEME_TYPE_PIFF, 0, "", &schi);
  }

  AP4_SyntheticSampleTable* sampleTable{new AP4_SyntheticSampleTable()};
  sampleTable->AddSampleDescription(sampleDesc);

  AP4_Movie* movie{new AP4_Movie()};
  movie->AddTrack(new AP4_Track(static_cast<AP4_Track::Type>(adStream.GetTrackType()), sampleTable,
                                AP4_TRACK_ID_UNKNOWN, repr->GetTimescale(), 0, repr->GetTimescale(),
                                0, "", 0, 0));
  // Create MOOV Atom to allow bento4 to handle stream as fragmented MP4
  AP4_MoovAtom* moov{new AP4_MoovAtom()};
  moov->AddChild(new AP4_ContainerAtom(AP4_ATOM_TYPE_MVEX));
  movie->SetMoovAtom(moov);
  return movie;
}
