/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "Session.h"
#include "utils/PropertiesUtils.h"

#include <kodi/addon-instance/Inputstream.h>
#include <kodi/addon-instance/VideoCodec.h>

/*******************************************************/
/*                     InputStream                     */
/*******************************************************/

class ATTR_DLL_LOCAL CInputStreamAdaptive : public kodi::addon::CInstanceInputStream
{
public:
  CInputStreamAdaptive(const kodi::addon::IInstanceInfo& instance);
  ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo& instance,
                              KODI_ADDON_INSTANCE_HDL& hdl) override;

  bool Open(const kodi::addon::InputstreamProperty& props) override;
  void Close() override;
  bool GetStreamIds(std::vector<unsigned int>& ids) override;
  void GetCapabilities(kodi::addon::InputstreamCapabilities& caps) override;
  bool GetStream(int streamid, kodi::addon::InputstreamInfo& info) override;
  void EnableStream(int streamid, bool enable) override;
  bool OpenStream(int streamid) override;
  DEMUX_PACKET* DemuxRead() override;
  bool DemuxSeekTime(double time, bool backwards, double& startpts) override;
  void SetVideoResolution(unsigned int width,
                          unsigned int height,
                          unsigned int maxWidth,
                          unsigned int maxHeight) override;
  bool PosTime(int ms) override;
  int GetTotalTime() override;
  int GetTime() override;
  bool IsRealTimeStream() override;

#if INPUTSTREAM_VERSION_LEVEL > 1
  int GetChapter() override;
  int GetChapterCount() override;
  const char* GetChapterName(int ch) override;
  int64_t GetChapterPos(int ch) override;
  bool SeekChapter(int ch) override;
#endif

  std::shared_ptr<SESSION::CSession> GetSession() { return m_session; };

private:
  std::shared_ptr<SESSION::CSession> m_session{nullptr};
  UTILS::PROPERTIES::KodiProperties m_kodiProps;
  int m_currentVideoWidth{0};
  int m_currentVideoHeight{0};
  int m_currentVideoMaxWidth{0};
  int m_currentVideoMaxHeight{0};
  uint32_t m_IncludedStreams[16];
  bool m_checkChapterSeek = false;
  int m_failedSeekTime = ~0;
  // The last PTS of the segment package fed to kodi.
  // NO_PTS_VALUE only when playback starts or a new period starts
  std::atomic<uint64_t> m_lastPts{adaptive::NO_PTS_VALUE};

  void UnlinkIncludedStreams(SESSION::CStream* stream);
};

/*******************************************************/
/*                     VideoCodec                      */
/*******************************************************/

class ATTR_DLL_LOCAL CVideoCodecAdaptive : public kodi::addon::CInstanceVideoCodec
{
public:
  CVideoCodecAdaptive(const kodi::addon::IInstanceInfo& instance);
  CVideoCodecAdaptive(const kodi::addon::IInstanceInfo& instance, CInputStreamAdaptive* parent);
  virtual ~CVideoCodecAdaptive();

  bool Open(const kodi::addon::VideoCodecInitdata& initData) override;
  bool Reconfigure(const kodi::addon::VideoCodecInitdata& initData) override;
  bool AddData(const DEMUX_PACKET& packet) override;
  VIDEOCODEC_RETVAL GetPicture(VIDEOCODEC_PICTURE& picture) override;
  const char* GetName() override { return m_name.c_str(); };
  void Reset() override;

private:
  enum STATE : unsigned int
  {
    STATE_WAIT_EXTRADATA = 1
  };

  std::shared_ptr<SESSION::CSession> m_session;
  unsigned int m_state;
  std::string m_name;
};
