/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "main.h"

#include "CompResources.h"
#include "CompKodiProps.h"
#include "SrvBroker.h"
#include "Stream.h"
#include "utils/GUIUtils.h"
#include "utils/ThreadPool.h"
#include "utils/log.h"

using namespace PLAYLIST;
using namespace SESSION;

CInputStreamAdaptive::CInputStreamAdaptive(const kodi::addon::IInstanceInfo& instance)
  : CInstanceInputStream(instance)
{
  CSrvBroker::GetInstance()->Initialize();
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

  CSrvBroker::GetInstance()->InitStage1(props.GetProperties());

  m_session = std::make_shared<CSession>();

  SResult ret = m_session->Initialize(props.GetURL());
  if (ret.IsFailed())
  {
    LOG::Log(LOGERROR, ret.Message().c_str());
    UTILS::GUI::ErrorDialog(ret.Message());
    m_session = nullptr;
    return false;
  }
  return true;
}

void CInputStreamAdaptive::Close(void)
{
  LOG::Log(LOGDEBUG, "Close()");
  m_session = nullptr;
  UTILS::THREAD::GlobalThreadPool.Reset();
  CSrvBroker::GetInstance()->Deinitialize();
}

bool CInputStreamAdaptive::GetStreamIds(std::vector<unsigned int>& ids)
{
  LOG::Log(LOGDEBUG, "GetStreamIds()");

  if (!m_session)
    return false;

  // You need to reset "stream open" count at each GetStreamIds callback
  // because after this event all streams will be opened
  m_streamOpenCount.clear();
  m_checkCoreReopen = false;

  const unsigned int streamCount = m_session->GetStreamCount();
  if (streamCount > INPUTSTREAM_MAX_STREAM_COUNT)
  {
    LOG::LogF(
        LOGWARNING,
        "Exceeded the maximum limit of %i streams. %u streams have been excluded from playback",
        INPUTSTREAM_MAX_STREAM_COUNT, streamCount - INPUTSTREAM_MAX_STREAM_COUNT);
  }

  for (unsigned int i(0); i < INPUTSTREAM_MAX_STREAM_COUNT && i < streamCount; ++i)
  {
    CStream* stream = m_session->GetStream(i);
    if (!stream)
    {
      LOG::LogF(LOGERROR, "Cannot get the stream from sid %u", i);
      continue;
    }

    if (stream->m_isValid &&
        (m_session->GetMediaTypeMask() &
         static_cast<uint8_t>(1) << static_cast<int>(stream->m_adStream.GetStreamType())))
    {
      if (m_session->GetMediaTypeMask() != 0xFF)
      {
        const CRepresentation* rep = stream->m_adStream.getRepresentation();
        if (rep->IsIncludedStream())
          continue;
      }

      const int id = m_session->GetStreamIdFromIndex(i);
      ids.emplace_back(id);
      m_streamOpenCount[id] = 0;
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
  // GetStream is called by Kodi twice times, before and after OpenStream.
  LOG::Log(LOGDEBUG, "GetStream(%d)", streamid);

  // Return false prevents this stream from loading, but does not stop playback,
  // Kodi core will continue to request another stream of same type (a/v)
  // as long as one is successful
  return m_session->OnGetStream(streamid, info);

  //! @todo: Kodi VideoPlayer stream fallback bug / problem:
  //! if a video stream for some reason does not start (OpenStream/GetStream fails) VideoPlayer has no logic to open the next video stream based of stream ID
  //! this is a big problem (can be reproduced using "Stream selection type" to "Manual OSD" and hack a bit the code)
  //! because VP seem to always take the first stream ID (1001) in index order, instead to continue with the next stream ID.
  //! For example if you are playing stream ID 1022 and fails, VP try to play always 1001 instead of 1023.
  //! This causes the following problems:
  //! - impossibility to keep a consistent video codec to be used
  //! - impossibility to have a stable video quality fallback in a decreasing manner. We could sort the list of streams decreasingly by resolution and bandwidth
  //!   but dont solve in full the problem in case of multicodec's because VP will mix all.
  //!   I'm talking about "decreasing manner" quality also because with DRM if 4k / FULLHD doesn't work you need a fallback to lower quality.
  //! this needs to be fixed in the Kodi core VP
}

void CInputStreamAdaptive::UnlinkIncludedStreams(CStream* stream)
{
  if (stream->m_mainStreamIndex.has_value())
  {
    CStream* mainStream(m_session->GetStream(*stream->m_mainStreamIndex));
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

  CStream* stream{m_session->GetStream(m_session->GetStreamIndexFromId(streamid))};

  if (!enable && stream && stream->m_isEnabled)
  {
    UnlinkIncludedStreams(stream);
    m_session->EnableStream(stream, false);
  }
}

// OpenStream method notes:
// - This method is called:
//    - At playback start
//    - At chapter/period change (DEMUX_SPECIALID_STREAMCHANGE)
//    - At stream quality change (DEMUX_SPECIALID_STREAMCHANGE) by "adaptive" streaming or from Kodi OSD
// - The "streamid" requested can be influenced from preferences set in Kodi settings (e.g. language).
// - If the requested "streamid" fails to open on the Kodi core side (after OpenStream callback, e.g. for missing extradata)
//   Kodi core will try to (fallback) open another video "streamid", this will happen recursively until success.
// - The OpenStream method not only opens the stream, but also implicitly enables it
//     so don't exists a EnableStream callback to explicitly enable the stream after the opening,
//     EnableStream method is used by VP only to disable the stream
//     which can happen immediately after opening (e.g. subtitles disabled on playback startup).
// - If a stream info property has been changed, you need to return "true" on OpenStream
//   to allow Kodi core to update its internal properties with our changes.
// - If you return "true" (it doesn't matter for which stream)
//   in any case will cause Kodi core to REOPEN ALL STREAMS TWICE TIMES,
//   so OpenStream method will be called again for all streams.
bool CInputStreamAdaptive::OpenStream(int streamid)
{
  LOG::Log(LOGDEBUG, "OpenStream(%d)", streamid);

  if (!m_session)
    return false;

  CStream* stream(m_session->GetStream(m_session->GetStreamIndexFromId(streamid)));

  if (!stream)
    return false;

  m_streamOpenCount[streamid]++;

  if (stream->m_adStream.StreamChanged())
  {
    UnlinkIncludedStreams(stream);
    stream->Reset();
    stream->m_adStream.Reset();
  }
  else
  {
    // Check to prevent Kodi core from attempting to open stream twice times (read OpenStream method note)
    if (m_checkCoreReopen && m_streamOpenCount[streamid] == 2)
    {
      LOG::Log(LOGDEBUG, "OpenStream(%d): The stream has already been opened", streamid);
      return false;
    }
  }

  stream->m_isEnabled = true;

  CRepresentation* rep = stream->m_adStream.getRepresentation();

  // If we select a dummy (=inside video) stream, open the video part
  // Dummy streams will be never enabled, they will only enable / activate audio track.
  if (rep->IsIncludedStream())
  {
    CStream* mainStream;
    unsigned int mainStreamIndex{0};

    while ((mainStream = m_session->GetStream(mainStreamIndex++)))
    {
      if (mainStream->m_info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO && mainStream->m_isEnabled)
        break;
    }

    if (mainStream)
    {
      stream->m_mainStreamIndex = mainStreamIndex;
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
      stream->m_mainStreamIndex.reset();
    }

    m_IncludedStreams[stream->m_info.GetStreamType()] = streamid;
    return false;
  }

  if (!m_session->PrepareStream(stream, m_lastPts))
  {
    m_session->EnableStream(stream, false);
    return false;
  }

  stream->GetReader()->SetStreamId(stream->m_info.GetStreamType(), streamid);

  if (stream->m_info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
  {
    for (auto& [streamType, id] : m_IncludedStreams)
    {
      stream->GetReader()->AddStreamType(streamType, id);

      const unsigned int streamIndex = m_session->GetStreamIndexFromId(id);

      CStream* incStream = m_session->GetStream(streamIndex);
      if (!incStream)
      {
        LOG::LogF(LOGERROR, "Cannot get the stream from stream index %u", streamIndex);
      }
      else
      {
        stream->GetReader()->GetInformation(incStream->m_info);
      }
    }
  }

  m_session->EnableStream(stream, true);

  // If stream use DRM always update stream info
  const bool isInfoChanged = stream->GetReader()->GetInformation(stream->m_info) ||
                             !stream->m_info.GetCryptoSession().GetSessionId().empty();

  if (isInfoChanged)
    m_checkCoreReopen = true;

  return isInfoChanged;
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
        m_lastPts = sr->DTSorPTSManifest();
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
    // Disable streams from the old period (kodi core never close/disable streams...)
    for (unsigned int i(0); i < INPUTSTREAM_MAX_STREAM_COUNT && i < m_session->GetStreamCount();
         ++i)
    {
      EnableStream(m_session->GetStreamIdFromIndex(i), false);
    }
    // Initialize the new period
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

void CInputStreamAdaptive::SetVideoResolution(unsigned int width,
                                              unsigned int height,
                                              unsigned int maxWidth,
                                              unsigned int maxHeight)
{
  CSrvBroker::GetResources().SetScreenInfo({static_cast<int>(width), static_cast<int>(height),
                                            static_cast<int>(maxWidth),
                                            static_cast<int>(maxHeight)});

  // SetVideoResolution method is initially called before CInputStreamAdaptive::Open so there is no session yet
  // After that, other callbacks may be made during playback (e.g. for window resize)
  if (m_session)
    m_session->OnScreenResChange();
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
  // When the addon is about to be terminated
  // CVideoCodecAdaptive instance will be destroyed before of CInputStreamAdaptive::Close() call
  LOG::Log(LOGDEBUG, "CVideoCodecAdaptive::~CVideoCodecAdaptive");
  m_drmDecoder->DisposeDecoder();
  m_drmDecoder = nullptr;
}

bool CVideoCodecAdaptive::Open(const kodi::addon::VideoCodecInitdata& initData)
{
  if (!m_session)
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

  const std::string sessionId = initData.GetCryptoSession().GetSessionId();

  auto drmSession = m_session->GetDRMEngine().GetSession(sessionId);
  if (!drmSession)
  {
    LOG::LogF(LOGERROR, "Cannot get DRM session id: %s", sessionId.c_str());
    return false;
  }

  m_drmDecoder = drmSession->drm;
  return m_drmDecoder->OpenVideoDecoder(drmSession->decrypter, initData.GetCStructure());
}

bool CVideoCodecAdaptive::Reconfigure(const kodi::addon::VideoCodecInitdata& initData)
{
  return false;
}

bool CVideoCodecAdaptive::AddData(const DEMUX_PACKET& packet)
{
  if (!m_drmDecoder)
    return false;

  return m_drmDecoder->DecryptAndDecodeVideo(dynamic_cast<kodi::addon::CInstanceVideoCodec*>(this),
                                      &packet) != VC_ERROR;
}

VIDEOCODEC_RETVAL CVideoCodecAdaptive::GetPicture(VIDEOCODEC_PICTURE& picture)
{
  if (!m_drmDecoder)
    return VIDEOCODEC_RETVAL::VC_ERROR;

  static VIDEOCODEC_RETVAL vrvm[] = {VIDEOCODEC_RETVAL::VC_NONE, VIDEOCODEC_RETVAL::VC_ERROR,
                                     VIDEOCODEC_RETVAL::VC_BUFFER, VIDEOCODEC_RETVAL::VC_PICTURE,
                                     VIDEOCODEC_RETVAL::VC_EOF};

  return vrvm[m_drmDecoder->VideoFrameDataToPicture(dynamic_cast<kodi::addon::CInstanceVideoCodec*>(this),
                                             &picture)];
}

void CVideoCodecAdaptive::Reset()
{
  if (!m_drmDecoder)
    return;

  m_drmDecoder->ResetVideo();
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
