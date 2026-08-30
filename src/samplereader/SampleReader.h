/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "decrypters/DrmEngineDefines.h"
#include "utils/CryptoUtils.h"
#include "utils/ThreadPool.h"

#include <bento4/Ap4.h>

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#include <kodi/addon-instance/Inputstream.h>
#endif

#include <cstdint>
#include <optional>
#include <future>

class Adaptive_CencSingleSampleDecrypter;

namespace SESSION
{
class CStream;
}

class ATTR_DLL_LOCAL SampleReaderObserver
{
public:
  /*!
   * \brief Callback raised when each fragment contained in a (fMP4) TFRF atom is parsed
   */
  virtual void OnTFRFatom(uint64_t ts, uint64_t duration, uint32_t mediaTimescale) = 0;
};

class ATTR_DLL_LOCAL ISampleReader
{
public:
  enum class Type
  {
    FMP4,
    TS,
    ADTS,
    WebM,
    Subtitles,
  };

  virtual ~ISampleReader() = default;

  virtual Type GetType() const = 0;

  virtual bool Initialize(SESSION::CStream* stream) { return true; }

  virtual std::vector<DRM::DRMInfo> GetInitDRMInfo() { return {}; }

  virtual void SetDecrypter(std::shared_ptr<DRM::DRMSession> drmSession)
  {
  }

  /*!
   * \brief Defines if the end of the stream is reached
   */
  virtual bool EOS() const = 0;
  /*!
   * \brief Defines if the sample reader is ready to process data,
   *        may be needed for streams that do not need to pause kodi VP buffer
   *        when there are no segments such as subtitles
   */
  virtual bool IsReady() { return true; }
  virtual uint64_t DTS() const = 0;
  virtual uint64_t PTS() const = 0;
  virtual uint64_t DTSorPTS() const { return DTS() < PTS() ? DTS() : PTS(); };
  /*!
   * \brief Start the reader by reading the first packet.
   * \param pts[OPT] The PTS where to start reading.
   * \return AP4_SUCCESS if reader is started or was already started, otherwise an error result.
   */
  virtual AP4_Result Start(std::optional<uint64_t> pts = std::nullopt) = 0;
  virtual AP4_Result ReadSample() = 0;
  virtual void Reset(bool bEOS) = 0;
  virtual bool GetInformation(kodi::addon::InputstreamInfo& info) = 0;

  /*!
   * \brief Advance the reader to requested PTS.
   * \param pts The PTS to seek for
   * \return True if has success, otherwise false.
   */
  virtual bool TimeSeek(uint64_t pts) = 0;

  /*!
   * \brief Seek the reader to an absolute reader PTS - the value domain returned by
   *        PTS() (native to this reader), not the manifest timing that TimeSeek() expects.
   *        Used after a seek to co-time a secondary stream (audio) with the video sample
   *        actually delivered: Kodi synchronises on the emitted PTS, and the audio/video
   *        share the source PTS clock, so aligning directly to the video's reader PTS avoids
   *        the drift between the audio and video manifest timelines that a manifest-domain
   *        seek (TimeSeek) reintroduces via GetPTSDiff().
   * \param pts The absolute reader PTS to align to.
   * \return True if has success, otherwise false.
   */
  virtual bool TimeSeekReaderPts(uint64_t pts)
  {
    // Default: remove the manifest compensation so TimeSeek lands on the given reader PTS.
    const int64_t manifestPts{static_cast<int64_t>(pts) - GetPTSDiff()};
    return TimeSeek(manifestPts < 0 ? 0 : static_cast<uint64_t>(manifestPts));
  }

  virtual void SetPTSOffset(uint64_t offset) = 0;
  virtual int64_t GetPTSDiff() const = 0;

  /*!
   * \brief Get the DTS or PTS of current packet, in manifest timing format
   *        (since packet DTS/PTS can be different from manifest PTS, and sliced).
   * \return The DTS/PTS in manifest timing format, scaled in STREAM_TIME_BASE.
   */
  virtual uint64_t DTSorPTSManifest() const {
    int64_t value = static_cast<int64_t>(DTSorPTS()) - GetPTSDiff();
    return value < 0 ? 0 : static_cast<uint64_t>(value);
  };

  /*!
   * \brief Read info about fragment on current segment (fMP4)
   * \param duration[OUT] Set the duration of current media sample
   * \return True if the fragment info was successfully retrieved, otherwise false
   */
  virtual bool GetFragmentInfo(uint64_t& duration) { return false; }

  virtual uint32_t GetTimeScale() const = 0;

  virtual int GetStreamId() const { return m_streamId; }
  virtual void SetStreamId(INPUTSTREAM_TYPE type, int streamId) { m_streamId = streamId; }

  virtual AP4_Size GetSampleDataSize() const = 0;
  virtual const AP4_Byte* GetSampleData() const = 0;
  virtual uint64_t GetDuration() const = 0;
  virtual bool IsEncrypted() const = 0;
  virtual bool IsStarted() const = 0;
  virtual CryptoInfo GetReaderCryptoInfo() const { return CryptoInfo(); }

  /*!
   * \brief Read the sample asynchronously
   */
  void ReadSampleAsync()
  {
    m_readSampleAsyncState =
        UTILS::THREAD::GlobalThreadPool.Execute(&ISampleReader::ReadSample, this);
  }

  /*!
   * \brief Wait for the asynchronous ReadSample to complete
   */
  void WaitReadSampleAsyncComplete()
  {
    if (m_readSampleAsyncState.valid())
      m_readSampleAsyncState.wait();
  }

  /*!
   * \brief Check if the async ReadSample is working
   * \return Return true if is working, otherwise false
   */
  bool IsReadSampleAsyncWorking()
  {
    return m_readSampleAsyncState.valid() &&
           m_readSampleAsyncState.wait_for(std::chrono::milliseconds(0)) !=
               std::future_status::ready;
  }

  void SetObserver(SampleReaderObserver* observer) { m_observer = observer; }

protected:
  SampleReaderObserver* m_observer{nullptr};

private:
  std::future<AP4_Result> m_readSampleAsyncState;
  int m_streamId{0};
};
