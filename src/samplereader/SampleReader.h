/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "utils/CryptoUtils.h"

#include <bento4/Ap4.h>

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#include <kodi/addon-instance/Inputstream.h>
#endif

#include <future>

class Adaptive_CencSingleSampleDecrypter;
namespace DRM
{
struct DecrypterCapabilites;
}
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
  virtual ~ISampleReader() = default;
  virtual bool Initialize(SESSION::CStream* stream) { return true; }
  virtual void SetDecrypter(Adaptive_CencSingleSampleDecrypter* ssd,
                            const DRM::DecrypterCapabilites& dcaps){};
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
  virtual AP4_Result Start(bool& bStarted) = 0;
  virtual AP4_Result ReadSample() = 0;
  virtual void Reset(bool bEOS) = 0;
  virtual bool GetInformation(kodi::addon::InputstreamInfo& info) = 0;
  virtual bool TimeSeek(uint64_t pts, bool preceeding) = 0;
  virtual void SetPTSOffset(uint64_t offset) = 0;
  virtual int64_t GetPTSDiff() const = 0;
  virtual void SetPTSDiff(uint64_t pts) {}

  /*!
   * \brief Read info about fragment on current segment (fMP4)
   * \param duration[OUT] Set the duration of current media sample
   * \return True if the fragment info was successfully retrieved, otherwise false
   */
  virtual bool GetFragmentInfo(uint64_t& duration) { return false; }

  virtual uint32_t GetTimeScale() const = 0;
  virtual AP4_UI32 GetStreamId() const = 0;
  virtual AP4_Size GetSampleDataSize() const = 0;
  virtual const AP4_Byte* GetSampleData() const = 0;
  virtual uint64_t GetDuration() const = 0;
  virtual bool IsEncrypted() const = 0;
  virtual void AddStreamType(INPUTSTREAM_TYPE type, uint32_t sid) {};
  virtual void SetStreamType(INPUTSTREAM_TYPE type, uint32_t sid) {};
  virtual bool RemoveStreamType(INPUTSTREAM_TYPE type) { return true; };
  virtual bool IsStarted() const = 0;
  virtual CryptoInfo GetReaderCryptoInfo() const { return CryptoInfo(); }

  /*!
   * \brief Read the sample asynchronously
   */
  void ReadSampleAsync()
  {
    m_readSampleAsyncState = std::async(std::launch::async, &ISampleReader::ReadSample, this);
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
};
