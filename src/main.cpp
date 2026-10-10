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
  LOG::Log(LOGWARNING, "CInputStreamAdaptive::CreateInstance");
  if (instance.IsType(ADDON_INSTANCE_VIDEOCODEC))
  {
    LOG::Log(LOGWARNING, "CInputStreamAdaptive::CreateInstance VIDEO");
    hdl = new CVideoCodecAdaptive(instance, this);
    return ADDON_STATUS_OK;
  }
  if (instance.IsType(ADDON_INSTANCE_AUDIOCODEC))
  {
    LOG::Log(LOGWARNING, "CInputStreamAdaptive::CreateInstance AUDIO");
    hdl = new CAudioCodecAdaptive(instance, this);
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
    auto stream = m_session->GetStream(i);
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

      ids.emplace_back(m_session->GetStreamIdFromIndex(i));
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

void CInputStreamAdaptive::EnableStream(int streamid, bool enable)
{
  LOG::Log(LOGDEBUG, "EnableStream(%d: %s)", streamid, enable ? "true" : "false");

  if (!m_session)
    return;

  auto stream = m_session->GetStream(m_session->GetStreamIndexFromId(streamid));

  if (!enable && stream && stream->IsEnabled())
  {
    stream->UnlinkStream();
    m_session->EnableStream(stream, false);
  }
}

//! @todo: InputStream "resume" playback design problem for multi-period/chapter streams case:
//! Currently, Kodi VideoPlayer operates in this order:
//!  1. "Open" callback (open the add-on)
//!  2. ... "GetStreamIds" callback, etc ...
//!  3. "OpenStream" callbacks (open the streams, and mandatory to provide the extradata when required)
//!  4. "PosTime" callback (in case of resuming video / seek stream operations)
//!  5. ... "DemuxRead" callbacks (start to feed data to Kodi core)
//!
//! As you can see from the flow when kodi request to open a stream "OpenStream" callbacks you have no way
//! to know if the playback is starting from the beginning or is resuming from a specific time position,
//! because "PosTime" callback is called only after "OpenStream" callbacks
//!
//! This lead that the add-on start to open all the streams from the beginning of the first chapter/period,
//! but if the playback is resuming from a chapter/period different from the first one, this is a big problem because we are
//! performing tons of useless work such as initialize unused streams, download and parse unused segment data, initialize decrypters...
//!
//! As workaround when we receive the "PosTime" callback with the position where to start the playback
//! we send the "DEMUX_SPECIALID_STREAMCHANGE" to VP to force VP to reopen all the streams from the right chapter/period,
//! but this is a bad design. We should open a stream from the appropriate chapter/period and position since the beginning
//! but we need a way to know if the playback is starting from the beginning or is resuming from a specific time position.
//! Related to this problem there is also another workaround applied on some sample readers that use
//! the AdaptiveStream::m_isAllowedBufferQueue to prevent to start downloading too much uneeded data.
//! Another side effect of "OpenStream" with TS/ADTS sample readers
//! since these streams dont have init segment, readers will parse a media segment to extract the stream info/extradata
//! but since we dont know the resume time, this segment is downloaded from the beginning of the first chapter/period
//! ofc this is not so good because cause network congestion (repeated downloads) and so worse execution time

// OpenStream method notes:
// - This method is called:
//    - At playback start
//    - At chapter/period change (DEMUX_SPECIALID_STREAMCHANGE)
//    - At stream quality change (DEMUX_SPECIALID_STREAMCHANGE) by "adaptive" streaming or from Kodi OSD
//    - Due to CDVDDemuxClient::ParsePacket (Kodi core) while in playback, see "Kodi core is attempting to reopen the stream" below
// - The "streamid" requested can be influenced from preferences set in Kodi settings (e.g. language).
// - If the requested "streamid" fails to open on the Kodi core side (after OpenStream callback, e.g. for missing extradata)
//   Kodi core will try to (fallback) open another video "streamid", this will happen recursively until success.
// - The OpenStream method not only opens the stream, but also implicitly enables it
//     so don't exists a EnableStream callback to explicitly enable the stream after the opening,
//     EnableStream method is used by VP only to disable the stream
//     which can happen immediately after opening (e.g. subtitles disabled on playback startup).
// - If a stream info property has been changed, you need to return "true" on OpenStream
//   to allow Kodi core to update its internal properties with our changes.
bool CInputStreamAdaptive::OpenStream(int streamid)
{
  LOG::Log(LOGDEBUG, "OpenStream(%d)", streamid);

  if (!m_session)
    return false;

  auto stream = m_session->GetStream(m_session->GetStreamIndexFromId(streamid));

  if (!stream)
    return false;

  const bool isStreamChanged{stream->m_adStream.StreamChanged()};

  if (stream->IsEnabled())
  {
    // Stream quality changed (by "adaptive" streaming, not OSD)
    if (isStreamChanged)
    {
      stream->Reset();
      stream->m_adStream.Reset();
    }
    else // Kodi core is attempting to reopen the stream
    {
      // If a stream was already opened means that Kodi core is attempting to reopen
      // ALL streams just for his internal purposes and this callback must be avoided.

      //! @todo: This issue appears to have been introduced with PR https://github.com/xbmc/xbmc/pull/10097
      //! when "changes" var differs from the stored value, it force to reopen all streams,
      //! where in the past was reopening only the associated stream.
      //!
      //! The request to reopen a stream is unclear,
      //! there is no reason to open a stream already opened, because the EnableStream(false) callback was never sent
      //! so the state of the addon is still unchanged.
      //!
      //! Looks like that the "changes" var is modified by CDVDDemuxClient::ParsePacket while in playback
      //! https://github.com/xbmc/xbmc/blob/d1a1d48c3cb3722d39264ffdd8132f755ffecd27/xbmc/cores/VideoPlayer/DVDDemuxers/DVDDemuxClient.cpp#L119
      //! maybe this is required for some other CDVDInputStream interface(?) but not for CInputStreamAddon, this should be at least optional
      //! since can easily leads to playback problems such as stuttering because reopening all streams can be an heavy task to do while playback
      //! moreover for subtitles case this is even more messed up, since they can be disabled... (EnableStream callback)

      // NOTE: you cannot rely on stream->IsEnabled() for all stream types because a playback can start with subtitles disabled
      // so this leads to a mess between OpenStream and EnableStream callbacks causing abnormal behaviors in the addon components.
      // As workaround we detect the first stream opened twice times (OpenStream for subtitles type is always last),
      // then we set m_checkCoreReopen to true in order to skip all the following callbacks to OpenStream,
      // and when we receive the first DemuxRead callback means that Kodi core has finished all openings,
      // then we reset m_checkCoreReopen for the next round of OpenStream callbacks
      m_checkCoreReopen = true;
    }
  }

  if (m_checkCoreReopen && !isStreamChanged)
  {
    LOG::Log(LOGDEBUG, "OpenStream(%d): The stream has already been opened", streamid);
    return false;
  }

  CRepresentation* rep = stream->m_adStream.getRepresentation();

  // "included" streams are audio streams embedded into a video stream (multiplexed).
  // How works:
  // The manifest parser create a "dummy" (AdaptationSet + Representation) for the audio track,
  // this dummy stream will be shared between all video representations
  // so needs to be linked to the video stream currently in use,
  // and if the video quality changes, must be re-linked with the current video.
  // The behavior of how works the "dummy" stream changes depending on the type of demuxer used,
  // in general way in this case the CStream dont use the AdaptiveStream class
  // but could have a SampleReader that somewhat read the data from the video package.
  //! @todo: HLS TS with multiple "included" audio streams, not implemented, more likely
  //!   its needed implement a way to get PIDs of each stream from segment, and map them to the manifest variants
  if (rep->IsIncludedStream())
  {
    auto videoStream = m_session->GetCurrentVideoStream();

    if (!videoStream)
    {
      LOG::LogF(LOGERROR, "Cannot get video stream to link the included stream id: %i",
                streamid);
      return false;
    }

    stream->LinkStream(videoStream, streamid);
    m_session->EnableStream(stream, true);
    return true;
  }

  if (!m_session->PrepareStream(*stream))
  {
    return false;
  }

  if (stream->GetReader()->GetType() == ISampleReader::Type::WebM &&
      !stream->m_info.GetCryptoSession().GetSessionId().empty())
  {
    //! @todo: it should be implemented on the WebM demuxer side and verify if appropriate changes are required on CDM
    LOG::LogF(LOGERROR, "WebM container with DRM encrypted stream is not supported");
    return false;
  }

  stream->GetReader()->SetStreamId(stream->m_info.GetStreamType(), streamid);

  // In case of video quality change (OSD)
  // re-link "included" streams to current opened stream
  // is assumed that "included" streams are still readable from the new selected stream
  if (stream->m_info.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
  {
    for (unsigned int i{0}; i < m_session->GetStreamCount(); ++i)
    {
      auto sesStream = m_session->GetStream(i);
      if (!sesStream || !sesStream->IsEnabled())
        continue;

      if (sesStream->IsLinkedToStreamType(INPUTSTREAM_TYPE_VIDEO))
      {
        sesStream->UnlinkStream();
        sesStream->LinkStream(stream, m_session->GetStreamIdFromIndex(i));
      }
    }
  }

  m_session->EnableStream(stream, true);

  // If stream use DRM always update stream info
  const bool isInfoChanged = stream->GetReader()->GetInformation(stream->m_info) ||
                             !stream->m_info.GetCryptoSession().GetSessionId().empty();
  return isInfoChanged;
}

DEMUX_PACKET* CInputStreamAdaptive::DemuxRead(void)
{
  if (!m_session)
    return NULL;

  m_session->OnDemuxRead();

  // On Kodi core CDVDDemuxClient::ParsePacket occurs after the DemuxRead callback
  // since it can cause to reopen all streams, reset the check just before
  m_checkCoreReopen = false;

  ISampleReader* sr{nullptr};

  if (m_session->GetNextSample(sr))
  {
    DEMUX_PACKET* p{nullptr};

    if (m_session->CheckChange())
    {
      // Adaptive stream has switched stream (representation) quality
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
  // Note: VP may require a seek (ms) even beyond the total duration of the media.
  // Note: When you return false VP will continue to request packets with DemuxRead callbacks
  // VP should expect to continue playback from the last PTS fed (as if the seek had not occurred)
  // or you can stop playback by forcibly stop the packet feed from DemuxRead callbacks.
  if (!m_session)
    return false;

  LOG::Log(LOGINFO, "PosTime (%d)", ms);

  const uint64_t currentTimeMs = m_session->GetElapsedTimeMs();
  bool isError{false};

  if (m_session->SeekTime(static_cast<double>(ms) * 0.001f, isError))
    return true;

  if (!isError)
  {
    // If for some reason the seek operation fails without errors
    // try to restore the streams/readers to previous (current) position
    LOG::Log(LOGWARNING, "PosTime - Seek failed. Attempt to restore previous %llu ms position",
             currentTimeMs);

    if (m_session->SeekTime(static_cast<double>(currentTimeMs) * 0.001f, isError))
      return false; // returns false because the initially requested seek was unsuccessful
  }

  // A problem has occurred or EOS, force stop playback
  LOG::Log(LOGDEBUG, "PosTime - Cannot seek at %d ms position.", ms);
  m_session->DeleteStreams();
  return false;
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
  // Provide the current chapter number
  // chapter numbering starts from 1, specify 0 for no chapter
  return m_session ? m_session->GetChapter() : 0;
}

int CInputStreamAdaptive::GetChapterCount()
{
  if (m_session)
  {
    const int count = m_session->GetChapterCount();
    if (count > 1)
      return count;
  }
  // Return 0 to prevent Kodi core from handling chapters
  return 0;
}

const char* CInputStreamAdaptive::GetChapterName(int ch)
{
  if (!m_session)
    return nullptr;

  return m_session->GetChapterName(ch);
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

/*******************************************************/
/*                     VideoCodec                      */
/*******************************************************/

CVideoCodecAdaptive::CVideoCodecAdaptive(const kodi::addon::IInstanceInfo& instance)
  : CInstanceVideoCodec(instance), m_name("inputstream.adaptive.decoder")
{
}

CVideoCodecAdaptive::CVideoCodecAdaptive(const kodi::addon::IInstanceInfo& instance,
                                         CInputStreamAdaptive* parent)
  : CInstanceVideoCodec(instance), m_session(parent->GetSession())
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
      initData.GetExtraDataSize() == 0 && !m_waitExtraData)
  {
    LOG::Log(LOGINFO, "VideoCodec::Open: Wait ExtraData");
    m_waitExtraData = true;
    return true;
  }
  m_waitExtraData = false;

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

/*******************************************************/
/*                     AudioCodec                      */
/*******************************************************/

CAudioCodecAdaptive::CAudioCodecAdaptive(const kodi::addon::IInstanceInfo& instance)
  : CInstanceAudioCodec(instance), m_name("inputstream.adaptive.decoder")
{
}

CAudioCodecAdaptive::CAudioCodecAdaptive(const kodi::addon::IInstanceInfo& instance,
                                         CInputStreamAdaptive* parent)
  : CInstanceAudioCodec(instance), m_session(parent->GetSession())
{
}

CAudioCodecAdaptive::~CAudioCodecAdaptive()
{
  // When the addon is about to be terminated
  // CAudioCodecAdaptive instance will be destroyed before of CInputStreamAdaptive::Close() call
  LOG::Log(LOGDEBUG, "CAudioCodecAdaptive::~CAudioCodecAdaptive");
  if (m_drmDecoderAudio)
    m_drmDecoderAudio->DisposeDecoder();

  m_drmDecoderAudio = nullptr;
}

bool CAudioCodecAdaptive::Open(const kodi::addon::AudioCodecInitdata& initData)
{
  if (!m_session)
    return false;

  LOG::Log(LOGINFO, "CAudioCodecAdaptive::Open");

  m_name = "inputstream.adaptive.audio.decoder";

  const std::string sessionId = initData.GetCryptoSession().GetSessionId();

  auto drmSession = m_session->GetDRMEngine().GetSession(sessionId);
  if (!drmSession)
  {
    LOG::LogF(LOGERROR, "Cannot get DRM session id: %s", sessionId.c_str());
    return false;
  }

  m_drmDecoderAudio = drmSession->drm;
  return m_drmDecoderAudio->OpenAudioDecoder(drmSession->decrypter, initData.GetCStructure());
}

bool CAudioCodecAdaptive::AddData(const DEMUX_PACKET& packet)
{
  if (!m_drmDecoderAudio)
    return false;

  return m_drmDecoderAudio->DecryptAndDecodeAudio(
             dynamic_cast<kodi::addon::CInstanceAudioCodec*>(this), &packet) != AC_ERROR;
}

AUDIOCODEC_RETVAL CAudioCodecAdaptive::GetFrame(AUDIOCODEC_FRAME& frame)
{
  if (!m_drmDecoderAudio)
    return AUDIOCODEC_RETVAL::AC_ERROR;

  static AUDIOCODEC_RETVAL arvm[] = {AUDIOCODEC_RETVAL::AC_NONE, AUDIOCODEC_RETVAL::AC_ERROR,
                                     AUDIOCODEC_RETVAL::AC_BUFFER, AUDIOCODEC_RETVAL::AC_FRAME,
                                     AUDIOCODEC_RETVAL::AC_EOF};

  return arvm[m_drmDecoderAudio->AudioFrameDataToFrame(
      dynamic_cast<kodi::addon::CInstanceAudioCodec*>(this), &frame)];
}

void CAudioCodecAdaptive::Reset()
{
  if (!m_drmDecoderAudio)
    return;

  m_drmDecoderAudio->ResetAudio();
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
