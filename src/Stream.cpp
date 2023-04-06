/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Stream.h"

using namespace SESSION;

void CStream::Disable()
{
  if (m_isEnabled)
  {
    // Stop downloads
    m_adStream.Stop();
    // ReadSample async thread may still working despite stop download signal
    // for example segmented webvtt use CSubtitleSampleReader -> retrieveCurrentSegmentBufferSize
    if (m_streamReader)
      m_streamReader->WaitReadSampleAsyncComplete();
    // Dispose the thread worker data only after async thread is complete otherwise mutex go to nirvana
    m_adStream.DisposeWorker();

    Reset();

    m_isEnabled = false;
    m_isEncrypted = false;
  }
}

void CStream::Reset()
{
  if (m_isEnabled)
  {
    if (m_streamReader)
      m_streamReader->WaitReadSampleAsyncComplete();
    m_streamReader.reset();
    m_streamFile.reset();
    m_adByteStream.reset();
    m_mainId = 0;
  }
}
