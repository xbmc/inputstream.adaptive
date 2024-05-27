/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "main.h"

#include "CompKodiProps.h"
#include "SrvBroker.h"
#include "Stream.h"
#include "samplereader/SampleReaderFactory.h"
#include "utils/log.h"

using namespace PLAYLIST;
using namespace SESSION;

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

  CSrvBroker::GetInstance()->Init(props.GetProperties());

  m_session = std::make_shared<CSession>(url);
  m_session->SetVideoResolution(m_currentVideoWidth, m_currentVideoHeight, m_currentVideoMaxWidth,
                                m_currentVideoMaxHeight);

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
          DRM::DecrypterCapabilites::SSD_SUPPORTS_DECODING)
        stream->m_info.SetFeatures(INPUTSTREAM_FEATURE_DECODE);
      else
        stream->m_info.SetFeatures(0);

      cryptoSession.SetFlags((m_session->GetDecrypterCaps(cdmId).flags &
                          DRM::DecrypterCapabilites::SSD_SECURE_DECODER)
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
  // This method can be called when:
  // - Stream first start
  // - Chapter/period change
  // - Automatic stream (representation) quality change (such as adaptive)
  // - Manual stream (representation) quality change (from OSD)
  // streamid behaviour:
  // - The streamid can be influenced by Kodi core based on preferences set in Kodi settings (e.g. language)
  // - Fallback calls, e.g. if the opened video streamid fails, Kodi core will try to open another video streamid

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

  stream->m_isEnabled = true;

  //! @todo: for live with multiple periods (like HLS DISCONTINUITIES) for subtitle case
  //! when the subtitle has been disabled from video start, and happens a period change,
  //! kodi call OpenStream to the subtitle stream as if it were enabled, and disable it with EnableStream
  //! just after it, this lead to log error "GenerateSidxSegments: [AS-x] Cannot generate segments from SIDX on repr..."
  //! need to find a solution to avoid open the stream when previously disabled from previous period

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

  m_session->PrepareStream(stream);

  stream->m_adStream.start_stream(m_lastPts);
  stream->SetAdByteStream(std::make_unique<CAdaptiveByteStream>(&stream->m_adStream));

  ContainerType reprContainerType = rep->GetContainerType();
  uint32_t mask = (1U << stream->m_info.GetStreamType()) | m_session->GetIncludedStreamMask();
  auto reader = ADP::CreateStreamReader(reprContainerType, stream, static_cast<uint32_t>(streamid), mask);

  if (!reader)
  {
    m_session->EnableStream(stream, false);
    return false;
  }

  uint16_t psshSetPos = stream->m_adStream.getRepresentation()->m_psshSetPos;
  reader->SetDecrypter(m_session->GetSingleSampleDecryptor(psshSetPos),
                       m_session->GetDecrypterCaps(psshSetPos));

  stream->SetReader(std::move(reader));

  if (reprContainerType == ContainerType::TS)
  {
    // With TS streams the elapsed time would be calculated incorrectly as during the tree refresh,
    // nextSegment would be deleted by the FreeSegments/newsegments swap. Do this now before the tree refresh.
    // Also, when reopening a stream (switching reps) the elapsed time would be incorrectly set until the
    // second segment plays, now force a correct calculation at the start of the stream.
    m_session->OnSegmentChanged(&stream->m_adStream);
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
  return stream->GetReader()->GetInformation(stream->m_info);
}


DEMUX_PACKET* CInputStreamAdaptive::DemuxRead(void)
{
  if (!m_session)
    return NULL;

  m_session->OnDemuxRead();

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
      // Adaptive stream has switched stream (representation) quality
      m_lastPts = PLAYLIST::NO_PTS_VALUE;
      p = AllocateDemuxPacket(0);
      p->iStreamId = DEMUX_SPECIALID_STREAMCHANGE;
      LOG::Log(LOGDEBUG, "DEMUX_SPECIALID_STREAMCHANGE (stream quality changed)");
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
        m_lastPts = sr->PTS();
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

  // Ends here when GetNextSample fails due to sample reader in EOS state or stream disabled
  // that could means, in case of multi-periods streams, that segments are ended
  // in the current period and its needed to switch to the next period
  if (m_session->SeekChapter(m_session->GetChapter() + 1))
  {
    // Switched to new period / chapter
    m_lastPts = PLAYLIST::NO_PTS_VALUE;
    for (unsigned int i(1);
         i <= INPUTSTREAM_MAX_STREAM_COUNT && i <= m_session->GetStreamCount(); ++i)
      EnableStream(i + m_session->GetPeriodId() * 1000, false);
    m_session->InitializePeriod();
    DEMUX_PACKET* p = AllocateDemuxPacket(0);
    p->iStreamId = DEMUX_SPECIALID_STREAMCHANGE;
    LOG::Log(LOGDEBUG, "DEMUX_SPECIALID_STREAMCHANGE (chapter changed)");
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
