/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../common/AdaptiveDecrypter.h"
#include "../utils/CryptoUtils.h"

#include <bento4/Ap4.h>

#ifdef INPUTSTREAM_TEST_BUILD
#include "../test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#include <kodi/addon-instance/Inputstream.h>
#endif

#include <future>

// Forward namespace/class
namespace SESSION
{
class CSession;
}

class ATTR_DLL_LOCAL ISampleReader
{
public:
  virtual ~ISampleReader() = default;
  virtual bool Initialize() { return true; };
  virtual bool EOS() const = 0;
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
  virtual bool GetNextFragmentInfo(uint64_t& ts, uint64_t& dur) = 0;
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

  /*
   * \brief Set the side data to the demux packet
   * \param pkt The packet where set the side data
   * \param session The current session
   */
  virtual void SetDemuxPacketSideData(DEMUX_PACKET* pkt, std::shared_ptr<SESSION::CSession> session)
  {
  }

private:
  std::future<AP4_Result> m_readSampleAsyncState;
};
