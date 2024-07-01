/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SampleReaderFactory.h"

#include "Stream.h"
#include "common/AdaptiveUtils.h"
#include "common/Representation.h"
#include "samplereader/ADTSSampleReader.h"
#include "samplereader/FragmentedSampleReader.h"
#include "samplereader/SubtitleSampleReader.h"
#include "samplereader/TSSampleReader.h"
#include "samplereader/WebmSampleReader.h"
#include "utils/log.h"

#include <bento4/Ap4.h>

using namespace PLAYLIST;

std::unique_ptr<ISampleReader> ADP::CreateStreamReader(PLAYLIST::ContainerType& containerType,
                                                       SESSION::CStream* stream,
                                                       uint32_t streamId,
                                                       uint32_t includedStreamMask)
{
  std::unique_ptr<ISampleReader> reader;

  if (containerType == ContainerType::TEXT)
  {
    reader = std::make_unique<CSubtitleSampleReader>(streamId);
  }
  else if (containerType == ContainerType::TS)
  {
    reader = std::make_unique<CTSSampleReader>(
        stream->GetAdByteStream(), stream->m_info.GetStreamType(), streamId, includedStreamMask);
  }
  else if (containerType == ContainerType::ADTS)
  {
    reader = std::make_unique<CADTSSampleReader>(stream->GetAdByteStream(), streamId);
  }
  else if (containerType == ContainerType::WEBM)
  {
    reader = std::make_unique<CWebmSampleReader>(stream->GetAdByteStream(), streamId);
  }
  else if (containerType == ContainerType::MP4)
  {
    // Note: AP4_Movie ptr will be deleted from AP4_File destructor of CStream
    AP4_Movie* movie{nullptr};
    if (stream->m_adStream.IsRequiredCreateMovieAtom() &&
        !stream->m_adStream.getRepresentation()->HasInitSegment())
    {
      movie = CreateMovieAtom(stream->m_adStream, stream->m_info);
    }
    // When "movie" is nullptr, AP4_File tries to extract it from the stream
    stream->SetStreamFile(std::make_unique<AP4_File>(
        *stream->GetAdByteStream(), AP4_DefaultAtomFactory::Instance_, true, movie));
    movie = stream->GetStreamFile()->GetMovie();

    if (!movie)
    {
      LOG::LogF(LOGERROR, "No MOOV atom in stream");
      return nullptr;
    }

    AP4_Track* track =
        movie->GetTrack(static_cast<AP4_Track::Type>(stream->m_adStream.GetTrackType()));
    if (!track)
    {
      if (stream->m_adStream.GetTrackType() == AP4_Track::TYPE_SUBTITLES)
        track = movie->GetTrack(AP4_Track::TYPE_TEXT);
      if (!track)
      {
        LOG::LogF(LOGERROR, "No suitable Track atom found in stream");
        return nullptr;
      }
    }

    if (!track->GetSampleDescription(0))
    {
      LOG::LogF(LOGERROR, "No STSD atom in stream");
      return nullptr;
    }

    reader = std::make_unique<CFragmentedSampleReader>(stream->GetAdByteStream(), movie, track,
                                                       streamId);
  }
  else
  {
    LOG::Log(LOGWARNING,
             "Cannot create sample reader due to unhandled representation container type");
    return nullptr;
  }

  if (!reader->Initialize(stream))
  {
    if (containerType == ContainerType::TS &&
        stream->m_adStream.GetStreamType() == StreamType::AUDIO)
    {
      // If TSSampleReader fail, try fallback to ADTS
      //! @todo: we should have an appropriate file type check
      //! e.g. with HLS we determine the container type from file extension
      //! in the url address, but .ts file could have ADTS
      LOG::LogF(LOGWARNING, "Cannot initialize TS sample reader, fallback to ADTS sample reader");

      containerType = ContainerType::ADTS;
      stream->m_adStream.getRepresentation()->SetContainerType(containerType);

      stream->GetAdByteStream()->Seek(0); // Seek because bytes are consumed from previous reader
      reader = std::make_unique<CADTSSampleReader>(stream->GetAdByteStream(), streamId);
      if (!reader->Initialize(stream))
        reader.reset();
    }
    else
      reader.reset();
  }

  return reader;
}
