/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "main.h"

#include "ADTSReader.h"
#include "Stream.h"
#include "TSReader.h"
#include "WebmReader.h"
#include "samplereader/ADTSSampleReader.h"
#include "samplereader/FragmentedSampleReader.h"
#include "samplereader/SubtitleSampleReader.h"
#include "samplereader/TSSampleReader.h"
#include "samplereader/WebmSampleReader.h"
#include "utils/Utils.h"
#include "utils/log.h"

#include <stdarg.h> // va_list, va_start, va_arg, va_end

using namespace PLAYLIST;
using namespace SESSION;
using namespace UTILS;

CInputStreamAdaptive::CInputStreamAdaptive(const kodi::addon::IInstanceInfo& instance)
  : CInstanceInputStream(instance)
{
}

ADDON_STATUS CInputStreamAdaptive::CreateInstance(const kodi::addon::IInstanceInfo& instance,
                                                  KODI_ADDON_INSTANCE_HDL& hdl)
{
  if (instance.IsType(ADDON_INSTANCE_VIDEOCODEC))
  {
    hdl = new CVideoCodecAdaptive(instance, this);
    return ADDON_STATUS_OK;
  }
  return ADDON_STATUS_NOT_IMPLEMENTED;
}

bool CInputStreamAdaptive::Open(const kodi::addon::InputstreamProperty& props)
{
  LOG::Log(LOGDEBUG, "Open()");

  std::string url = props.GetURL();
  m_kodiProps = PROPERTIES::ParseKodiProperties(props.GetProperties());

  std::uint8_t drmConfig{0};
  if (m_kodiProps.m_isLicensePersistentStorage)
    drmConfig |= DRM::IDecrypter::CONFIG_PERSISTENTSTORAGE;

  m_session = std::make_shared<CSession>(m_kodiProps, url, props.GetProfileFolder());
  m_session->SetVideoResolution(m_currentVideoWidth, m_currentVideoHeight, m_currentVideoMaxWidth,
                                m_currentVideoMaxHeight);

  m_session->SetDrmConfig(drmConfig);
  if (!m_session->Initialize())
  {
    m_session = nullptr;
    return false;
  }
  return true;
}

void CInputStreamAdaptive::Close(void)
{
  LOG::Log(LOGDEBUG, "Close()");
  m_session = nullptr;
}

bool CInputStreamAdaptive::GetStreamIds(std::vector<unsigned int>& ids)
{
  LOG::Log(LOGDEBUG, "GetStreamIds()");
  INPUTSTREAM_IDS iids;

  if (m_session)
  {
    CPeriod* period;
    int period_id = m_session->GetPeriodId();
    iids.m_streamCount = 0;
    unsigned int id;

    for (unsigned int i(1); i <= INPUTSTREAM_MAX_STREAM_COUNT && i <= m_session->GetStreamCount();
         ++i)
    {
      CStream* stream = m_session->GetStream(i);
      if (!stream)
      {
        LOG::LogF(LOGERROR, "Cannot get the stream from sid %u", i);
        continue;
      }

      uint8_t cdmId(static_cast<uint8_t>(stream->m_adStream.getRepresentation()->m_psshSetPos));
      if (stream->m_isValid && (m_session->GetMediaTypeMask() &
                                static_cast<uint8_t>(1) << static_cast<int>(stream->m_adStream.GetStreamType())))
      {
        if (m_session->GetMediaTypeMask() != 0xFF)
        {
          const CRepresentation* rep = stream->m_adStream.getRepresentation();
          if (rep->IsIncludedStream())
            continue;
        }
        if (m_session->IsLive())
        {
          period = stream->m_adStream.getPeriod();
          if (m_session->HasInitialSequence() &&
              period->GetSequence() == m_session->GetInitialSequence())
          {
            id = i + 1000;
          }
          else
          {
            id = i + (period->GetSequence() + 1) * 1000;
          }
        }
        else
        {
          id = i + period_id * 1000;
        }
        ids.emplace_back(id);
      }
    }
  }

  return !ids.empty();
}

void CInputStreamAdaptive::GetCapabilities(kodi::addon::InputstreamCapabilities& caps)
{
  LOG::Log(LOGDEBUG, "GetCapabilities()");
  uint32_t mask = INPUTSTREAM_SUPPORTS_IDEMUX | INPUTSTREAM_SUPPORTS_IDISPLAYTIME |
                  INPUTSTREAM_SUPPORTS_IPOSTIME | INPUTSTREAM_SUPPORTS_SEEK |
                  INPUTSTREAM_SUPPORTS_PAUSE;
#if INPUTSTREAM_VERSION_LEVEL > 1
  mask |= INPUTSTREAM_SUPPORTS_ICHAPTER;
#endif
  caps.SetMask(mask);
}

bool CInputStreamAdaptive::GetStream(int streamid, kodi::addon::InputstreamInfo& info)
{
  LOG::Log(LOGDEBUG, "GetStream(%d)", streamid);

  CStream* stream(m_session->GetStream(streamid - m_session->GetPeriodId() * 1000));

  if (stream)
  {
    uint8_t cdmId(static_cast<uint8_t>(stream->m_adStream.getRepresentation()->m_psshSetPos));
    if (stream->m_isEncrypted && m_session->GetCDMSession(cdmId) != nullptr)
    {
      kodi::addon::StreamCryptoSession cryptoSession;

      LOG::Log(LOGDEBUG, "GetStream(%d): initalizing crypto session", streamid);
      cryptoSession.SetKeySystem(m_session->GetCryptoKeySystem());

      const char* sessionId(m_session->GetCDMSession(cdmId));
      cryptoSession.SetSessionId(sessionId);

      if (m_session->GetDecrypterCaps(cdmId).flags &
          DRM::IDecrypter::DecrypterCapabilites::SSD_SUPPORTS_DECODING)
        stream->m_info.SetFeatures(INPUTSTREAM_FEATURE_DECODE);
      else
        stream->m_info.SetFeatures(0);

      cryptoSession.SetFlags((m_session->GetDecrypterCaps(cdmId).flags &
                          DRM::IDecrypter::DecrypterCapabilites::SSD_SECURE_DECODER)
                             ? STREAM_CRYPTO_FLAG_SECURE_DECODER
                             : 0);
      stream->m_info.SetCryptoSession(cryptoSession);
    }

    info = stream->m_info;
    return true;
  }

  return false;
}

void CInputStreamAdaptive::UnlinkIncludedStreams(CStream* stream)
{
  if (stream->m_mainId)
  {
    CStream* mainStream(m_session->GetStream(stream->m_mainId));
    if (mainStream->GetReader())
      mainStream->GetReader()->RemoveStreamType(stream->m_info.GetStreamType());
  }

  const CRepresentation* rep = stream->m_adStream.getRepresentation();

  if (rep->IsIncludedStream())
    m_IncludedStreams.erase(stream->m_info.GetStreamType());
}

void CInputStreamAdaptive::EnableStream(int streamid, bool enable)
{
  LOG::Log(LOGDEBUG, "EnableStream(%d: %s)", streamid, enable ? "true" : "false");

  if (!m_session)
    return;

  CStream* stream(m_session->GetStream(streamid - m_session->GetPeriodId() * 1000));

  if (!enable && stream && stream->m_isEnabled)
  {
    UnlinkIncludedStreams(stream);
    m_session->EnableStream(stream, false);
  }
}

// If we change some Kodi stream property we must to return true
// to allow Kodi demuxer to reset with our changes the stream properties.
bool CInputStreamAdaptive::OpenStream(int streamid)
{
  LOG::Log(LOGDEBUG, "OpenStream(%d)", streamid);

  if (!m_session)
    return false;

  CStream* stream(m_session->GetStream(streamid - m_session->GetPeriodId() * 1000));

  if (!stream)
    return false;

  if (stream->m_isEnabled)
  {
    if (stream->m_adStream.StreamChanged())
    {
      UnlinkIncludedStreams(stream);
      stream->Reset();
      stream->m_adStream.Reset();
    }
    else
      return false;
  }

  bool needRefetch = false; //Make sure that Kodi fetches changes
  stream->m_isEnabled = true;

  CRepresentation* rep = stream->m_adStream.getRepresentation();

  // If we select a dummy (=inside video) stream, open the video part
  // Dummy streams will be never enabled, they will only enable / activate audio track.
  if (rep->IsIncludedStream())
  {
    CStream* mainStream;
    stream->m_mainId = 0;
    while ((mainStream = m_session->GetStream(++stream->m_mainId)))
      if (mainStream->m_info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO && mainStream->m_isEnabled)
        break;
    if (mainStream)
    {
      ISampleReader* mainReader = mainStream->GetReader();
      if (!mainReader)
      {
        LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
      }
      else
      {
        mainReader->AddStreamType(stream->m_info.GetStreamType(), streamid);
        mainReader->GetInformation(stream->m_info);
      }
    }
    else
    {
      stream->m_mainId = 0;
    }
    m_IncludedStreams[stream->m_info.GetStreamType()] = streamid;
    return false;
  }

  if (rep->IsSubtitleFileStream())
  {
    stream->SetReader(std::make_unique<CSubtitleSampleReader>(
        rep->GetBaseUrl(), streamid, stream->m_info.GetCodecInternalName(),
        stream->m_adStream.GetStreamParams(), stream->m_adStream.GetStreamHeaders()));
    return stream->GetReader()->GetInformation(stream->m_info);
  }

  m_session->PrepareStream(stream, needRefetch);

  stream->m_adStream.start_stream();

  ContainerType reprContainerType = rep->GetContainerType();

  if (reprContainerType == ContainerType::TEXT)
  {
    stream->SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream->m_adStream));
    stream->SetReader(std::make_unique<CSubtitleSampleReader>(
        stream, streamid, stream->m_info.GetCodecInternalName()));
  }
  else if (reprContainerType == ContainerType::TS)
  {
    stream->SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream->m_adStream));

    uint32_t mask{(1U << stream->m_info.GetStreamType()) | m_session->GetIncludedStreamMask()};
    stream->SetReader(std::make_unique<CTSSampleReader>(
        stream->GetAdByteStream(), stream->m_info.GetStreamType(), streamid, mask));

    if (stream->GetReader()->Initialize())
    {
      m_session->OnSegmentChanged(&stream->m_adStream);
    }
    else if (stream->m_adStream.GetStreamType() == StreamType::AUDIO)
    {
      // If TSSampleReader fail, try fallback to ADTS
      //! @todo: we should have an appropriate file type check
      //! e.g. with HLS we determine the container type from file extension
      //! in the url address, but .ts file could have ADTS
      LOG::LogF(LOGWARNING, "Cannot initialize TS sample reader, fallback to ADTS sample reader");
      rep->SetContainerType(ContainerType::ADTS);

      stream->GetAdByteStream()->Seek(0); // Seek because bytes are consumed from previous reader
      stream->SetReader(std::make_unique<CADTSSampleReader>(stream->GetAdByteStream(), streamid));
    }
    else
    {
      stream->Disable();
      return false;
    }
  }
  else if (reprContainerType == ContainerType::ADTS)
  {
    stream->SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream->m_adStream));
    stream->SetReader(std::make_unique<CADTSSampleReader>(stream->GetAdByteStream(), streamid));
  }
  else if (reprContainerType == ContainerType::WEBM)
  {
    stream->SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream->m_adStream));
    stream->SetReader(std::make_unique<CWebmSampleReader>(stream->GetAdByteStream(), streamid));
    if (!stream->GetReader()->Initialize())
    {
      stream->Disable();
      return false;
    }
  }
  else if (reprContainerType == ContainerType::MP4)
  {
    AP4_Movie* movie{nullptr};
    if (stream->m_adStream.IsRequiredCreateMovieAtom())
      movie = m_session->CreateMovieAtom(stream);

    stream->SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream->m_adStream));
    // When "movie" is nullptr, AP4_File tries to extract it from the stream
    stream->SetStreamFile(std::make_unique<AP4_File>(
        *stream->GetAdByteStream(), AP4_DefaultAtomFactory::Instance_, true, movie));
    movie = stream->GetStreamFile()->GetMovie();

    if (!movie)
    {
      LOG::LogF(LOGERROR, "No MOOV atom in stream");
      m_session->EnableStream(stream, false);
      return false;
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
        m_session->EnableStream(stream, false);
        return false;
      }
    }

    if (!track->GetSampleDescription(0))
    {
      LOG::LogF(LOGERROR, "No STSD atom in stream");
      m_session->EnableStream(stream, false);
      return false;
    }

    auto sampleDecrypter =
        m_session->GetSingleSampleDecryptor(stream->m_adStream.getRepresentation()->m_psshSetPos);
    auto caps = m_session->GetDecrypterCaps(stream->m_adStream.getRepresentation()->m_psshSetPos);

    stream->SetReader(std::make_unique<CFragmentedSampleReader>(
        stream->GetAdByteStream(), movie, track, streamid, sampleDecrypter, caps));
  }
  else
  {
    LOG::LogF(LOGWARNING, "Unhandled stream container for representation ID: %s", rep->GetId().data());
    m_session->EnableStream(stream, false);
    return false;
  }

  if (stream->m_info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
  {
    for (auto& [streamType, id] : m_IncludedStreams)
    {
      stream->GetReader()->AddStreamType(streamType, id);

      unsigned int sid = id - m_session->GetPeriodId() * 1000;

      CStream* incStream = m_session->GetStream(sid);
      if (!incStream)
      {
        LOG::LogF(LOGERROR, "Cannot get the stream from sid %u", sid);
      }
      else
      {
        stream->GetReader()->GetInformation(incStream->m_info);
      }
    }
  }
  m_session->EnableStream(stream, true);
  return stream->GetReader()->GetInformation(stream->m_info) || needRefetch;
}


DEMUX_PACKET* CInputStreamAdaptive::DemuxRead(void)
{
  if (!m_session)
    return NULL;

  if (m_checkChapterSeek)
  {
    m_checkChapterSeek = false;
    if (m_session->GetChapterSeekTime() > 0)
    {
      m_session->SeekTime(m_session->GetChapterSeekTime());
      m_session->ResetChapterSeekTime();
    }
  }

  if (~m_failedSeekTime)
  {
    LOG::Log(LOGDEBUG, "Seeking to last failed seek position (%d)", m_failedSeekTime);
    m_session->SeekTime(static_cast<double>(m_failedSeekTime) * 0.001f, 0, false);
    m_failedSeekTime = ~0;
  }

  ISampleReader* sr{nullptr};

  if (m_session->GetNextSample(sr))
  {
    DEMUX_PACKET* p{nullptr};

    if (m_session->CheckChange())
    {
      p = AllocateDemuxPacket(0);
      p->iStreamId = DEMUX_SPECIALID_STREAMCHANGE;
      LOG::Log(LOGDEBUG, "DEMUX_SPECIALID_STREAMCHANGE");
      return p;
    }

    if (sr)
    {
      AP4_Size iSize(sr->GetSampleDataSize());
      const AP4_UI08* pData(sr->GetSampleData());
      bool srHaveData{iSize > 0 && pData};

      if (sr->IsEncrypted() && srHaveData)
      {
        const unsigned int numSubSamples(*(reinterpret_cast<const unsigned int*>(pData)));
        pData += sizeof(numSubSamples);
        p = AllocateEncryptedDemuxPacket(iSize, numSubSamples);
        std::memcpy(p->cryptoInfo->clearBytes, pData, numSubSamples * sizeof(uint16_t));
        pData += (numSubSamples * sizeof(uint16_t));
        std::memcpy(p->cryptoInfo->cipherBytes, pData, numSubSamples * sizeof(uint32_t));
        pData += (numSubSamples * sizeof(uint32_t));
        std::memcpy(p->cryptoInfo->iv, pData, 16);
        pData += 16;
        std::memcpy(p->cryptoInfo->kid, pData, 16);
        pData += 16;
        iSize -= static_cast<AP4_Size>(pData - sr->GetSampleData());
        CryptoInfo cryptoInfo = sr->GetReaderCryptoInfo();
        p->cryptoInfo->numSubSamples = numSubSamples;
        p->cryptoInfo->cryptBlocks = cryptoInfo.m_cryptBlocks;
        p->cryptoInfo->skipBlocks = cryptoInfo.m_skipBlocks;
        p->cryptoInfo->mode = static_cast<uint16_t>(cryptoInfo.m_mode);
        p->cryptoInfo->flags = 0;
      }
      else
        p = AllocateDemuxPacket(iSize);

      if (srHaveData)
      {
        p->dts = static_cast<double>(sr->DTS());
        p->pts = static_cast<double>(sr->PTS());
        p->duration = static_cast<double>(sr->GetDuration());
        p->iStreamId = sr->GetStreamId();
        p->iGroupId = 0;
        p->iSize = iSize;
        std::memcpy(p->pData, pData, iSize);
      }

      //LOG::Log(LOGDEBUG, "DTS: %0.4f, PTS:%0.4f, ID: %u SZ: %d", p->dts, p->pts, p->iStreamId, p->iSize);

      // Start reading the next sample
      sr->ReadSampleAsync();
    }
    else // We are waiting for the data, so return an empty packet
    {
      p = AllocateDemuxPacket(0);
    }

    return p;
  }

  if (m_session->SeekChapter(m_session->GetChapter() + 1))
  {
    m_checkChapterSeek = true;
    for (unsigned int i(1);
         i <= INPUTSTREAM_MAX_STREAM_COUNT && i <= m_session->GetStreamCount(); ++i)
      EnableStream(i + m_session->GetPeriodId() * 1000, false);
    m_session->InitializePeriod();
    DEMUX_PACKET* p = AllocateDemuxPacket(0);
    p->iStreamId = DEMUX_SPECIALID_STREAMCHANGE;
    LOG::Log(LOGDEBUG, "DEMUX_SPECIALID_STREAMCHANGE");
    return p;
  }
  return NULL;
}

// Accurate search (PTS based)
bool CInputStreamAdaptive::DemuxSeekTime(double time, bool backwards, double& startpts)
{
  return true;
}

// Kodi callback, called just before CInputStreamAdaptive::Open method,
// and every time the resolution change while in playback (e.g. window resize)
void CInputStreamAdaptive::SetVideoResolution(unsigned int width,
                                              unsigned int height,
                                              unsigned int maxWidth,
                                              unsigned int maxHeight)
{
  m_currentVideoWidth = static_cast<int>(width);
  m_currentVideoHeight = static_cast<int>(height);
  m_currentVideoMaxWidth = static_cast<int>(maxWidth);
  m_currentVideoMaxHeight = static_cast<int>(maxHeight);

  // This can be called just after CInputStreamAdaptive::Open callback
  if (m_session)
     m_session->SetVideoResolution(width, height, maxWidth, maxHeight);
}

bool CInputStreamAdaptive::PosTime(int ms)
{
  if (!m_session)
    return false;

  LOG::Log(LOGINFO, "PosTime (%d)", ms);

  bool ret = m_session->SeekTime(static_cast<double>(ms) * 0.001f, 0, false);
  m_failedSeekTime = ret ? ~0 : ms;

  return ret;
}

int CInputStreamAdaptive::GetTotalTime()
{
  if (!m_session)
    return 0;

  return static_cast<int>(m_session->GetTotalTimeMs());
}

int CInputStreamAdaptive::GetTime()
{
  if (!m_session)
    return 0;

  int timeMs = static_cast<int>(m_session->GetElapsedTimeMs());
  return timeMs;
}

bool CInputStreamAdaptive::IsRealTimeStream()
{
  return m_session && m_session->IsLive();
}

#if INPUTSTREAM_VERSION_LEVEL > 1
int CInputStreamAdaptive::GetChapter()
{
  return m_session ? m_session->GetChapter() : 0;
}

int CInputStreamAdaptive::GetChapterCount()
{
  return m_session ? m_session->GetChapterCount() : 0;
}

const char* CInputStreamAdaptive::GetChapterName(int ch)
{
  if (!m_session)
    return nullptr;

  //! @todo: m_chapterName is a workaround fix for compiler
  //! "warning: returning address of local temporary object"
  //! we have to store the chapter name locally because the pointer returned is used after
  //! that Kodi make the GetChapterName callback, so it go out of scope. A way to fix this
  //! is pass the char pointer by using "strdup", but is needed that when kodi make
  //! GetChapterName callback also "free" the value after his use.
  m_chapterName = m_session->GetChapterName(ch);
  return m_chapterName.c_str();
}

int64_t CInputStreamAdaptive::GetChapterPos(int ch)
{
  return m_session ? m_session->GetChapterPos(ch) : 0;
}

bool CInputStreamAdaptive::SeekChapter(int ch)
{
  return m_session ? m_session->SeekChapter(ch) : false;
}
#endif
/*****************************************************************************************************/

CVideoCodecAdaptive::CVideoCodecAdaptive(const kodi::addon::IInstanceInfo& instance)
  : CInstanceVideoCodec(instance),
    m_session(nullptr),
    m_state(0),
    m_name("inputstream.adaptive.decoder")
{
}

CVideoCodecAdaptive::CVideoCodecAdaptive(const kodi::addon::IInstanceInfo& instance,
                                         CInputStreamAdaptive* parent)
  : CInstanceVideoCodec(instance), m_session(parent->GetSession()), m_state(0)
{
}

CVideoCodecAdaptive::~CVideoCodecAdaptive()
{
}

bool CVideoCodecAdaptive::Open(const kodi::addon::VideoCodecInitdata& initData)
{
  if (!m_session || !m_session->GetDecrypter())
    return false;
 
  if ((initData.GetCodecType() == VIDEOCODEC_H264 || initData.GetCodecType() == VIDEOCODEC_AV1) &&
      !initData.GetExtraDataSize() && !(m_state & STATE_WAIT_EXTRADATA))
  {
    LOG::Log(LOGINFO, "VideoCodec::Open: Wait ExtraData");
    m_state |= STATE_WAIT_EXTRADATA;
    return true;
  }
  m_state &= ~STATE_WAIT_EXTRADATA;

  LOG::Log(LOGINFO, "VideoCodec::Open");

  m_name = "inputstream.adaptive";
  switch (initData.GetCodecType())
  {
    case VIDEOCODEC_VP8:
      m_name += ".vp8";
      break;
    case VIDEOCODEC_H264:
      m_name += ".h264";
      break;
    case VIDEOCODEC_VP9:
      m_name += ".vp9";
      break;
    case VIDEOCODEC_AV1:
      m_name += ".av1";
      break;
    default:
      break;
  }
  m_name += ".decoder";

  std::string sessionId(initData.GetCryptoSession().GetSessionId());
  Adaptive_CencSingleSampleDecrypter* ssd(m_session->GetSingleSampleDecrypter(sessionId));

  return m_session->GetDecrypter()->OpenVideoDecoder(
      ssd, initData.GetCStructure());
}

bool CVideoCodecAdaptive::Reconfigure(const kodi::addon::VideoCodecInitdata& initData)
{
  return false;
}

bool CVideoCodecAdaptive::AddData(const DEMUX_PACKET& packet)
{
  if (!m_session || !m_session->GetDecrypter())
    return false;

  return m_session->GetDecrypter()->DecryptAndDecodeVideo(
             dynamic_cast<kodi::addon::CInstanceVideoCodec*>(this), &packet) != VC_ERROR;
}

VIDEOCODEC_RETVAL CVideoCodecAdaptive::GetPicture(VIDEOCODEC_PICTURE& picture)
{
  if (!m_session || !m_session->GetDecrypter())
    return VIDEOCODEC_RETVAL::VC_ERROR;

  static VIDEOCODEC_RETVAL vrvm[] = {VIDEOCODEC_RETVAL::VC_NONE, VIDEOCODEC_RETVAL::VC_ERROR,
                                     VIDEOCODEC_RETVAL::VC_BUFFER, VIDEOCODEC_RETVAL::VC_PICTURE,
                                     VIDEOCODEC_RETVAL::VC_EOF};

  return vrvm[m_session->GetDecrypter()->VideoFrameDataToPicture(
      dynamic_cast<kodi::addon::CInstanceVideoCodec*>(this), &picture)];
}

void CVideoCodecAdaptive::Reset()
{
  if (!m_session || !m_session->GetDecrypter())
    return;

  m_session->GetDecrypter()->ResetVideo();
}

/*****************************************************************************************************/

class ATTR_DLL_LOCAL CMyAddon : public kodi::addon::CAddonBase
{
public:
  CMyAddon();
  ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo& instance,
                              KODI_ADDON_INSTANCE_HDL& hdl) override;
};

CMyAddon::CMyAddon()
{
}

ADDON_STATUS CMyAddon::CreateInstance(const kodi::addon::IInstanceInfo& instance,
                                      KODI_ADDON_INSTANCE_HDL& hdl)
{
  if (instance.IsType(ADDON_INSTANCE_INPUTSTREAM))
  {
    hdl = new CInputStreamAdaptive(instance);
    return ADDON_STATUS_OK;
  }
  return ADDON_STATUS_NOT_IMPLEMENTED;
}

ADDONCREATOR(CMyAddon);
