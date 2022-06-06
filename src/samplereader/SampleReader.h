/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../AdaptiveByteStream.h"
#include "../common/AdaptiveDecrypter.h"

#include <bento4/Ap4.h>

#ifdef INPUTSTREAM_TEST_BUILD
#include "../test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#include <kodi/addon-instance/Inputstream.h>
#endif

struct ReaderCryptoInfo
{
  uint8_t m_cryptBlocks{0};
  uint8_t m_skipBlocks{0};
  CryptoMode m_mode{CryptoMode::NONE};
};

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
  virtual ReaderCryptoInfo GetReaderCryptoInfo() const { return ReaderCryptoInfo(); }
};
