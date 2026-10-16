/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Stream.h"

#include "samplereader/FragmentedSampleReader.h"
#include "samplereader/TSSampleReader.h"
#include "utils/log.h"

using namespace SESSION;

void CStream::Disable()
{
  m_adStream.Disable();
  // Stop method stop the downloads, but not the reader.
  m_adStream.Stop();
  // The reader is an async thread and may still working to read buffer data,
  // so wait for it to exit before the AdaptiveStream::Dispose call,
  // otherwise it will cause data access violation on buffers because still in use
  if (m_streamReader)
    m_streamReader->WaitReadSampleAsyncComplete();

  m_adStream.Dispose();

  Reset();

  m_isEnabled = false;
}

void CStream::Reset()
{
  m_isEnabled = false;
  m_lastEmittedDtsManifest.reset();

  if (m_streamReader)
    m_streamReader->WaitReadSampleAsyncComplete();
  m_streamReader.reset();
  m_streamFile.reset();
  m_adByteStream.reset();
}

void SESSION::CStream::SetReader(std::unique_ptr<ISampleReader> reader)
{
  m_lastEmittedDtsManifest.reset();
  m_streamReader = std::move(reader);
  m_streamReader->SetObserver(&m_adStream);
}

void SESSION::CStream::LinkStream(std::shared_ptr<CStream> mainStream, int streamId)
{
  m_lastEmittedDtsManifest.reset();
  ISampleReader* mainStreamReader = mainStream->GetReader();
  if (!mainStreamReader)
  {
    LOG::LogF(LOGERROR, "Cannot link stream ID %i, due to missing sample reader on main stream",
              streamId);
    return;
  }

  if (mainStreamReader->GetType() == ISampleReader::Type::TS)
  {
    auto tsReader = static_cast<CTSSampleReader*>(mainStreamReader);
    tsReader->AddStreamType(m_info.GetStreamType(), streamId);
    tsReader->GetInformation(m_info);
  }
  else if (mainStreamReader->GetType() == ISampleReader::Type::FMP4)
  {
    auto fMainReader = static_cast<CFragmentedSampleReader*>(mainStreamReader);

    m_streamReader = fMainReader->CreateReaderByTrack();
    if (!m_streamReader)
      return;

    m_streamReader->Initialize(this);
    m_streamReader->SetStreamId(m_info.GetStreamType(), streamId);
    m_streamReader->GetInformation(m_info);
  }
  else
  {
    LOG::LogF(LOGERROR, "Cannot link stream ID %i, due to unsupported sample reader type: %i",
              streamId, mainStreamReader->GetType());
    return;
  }

  m_linkedStream = mainStream;
}

void SESSION::CStream::UnlinkStream()
{
  m_lastEmittedDtsManifest.reset();
  if (auto linkStream = m_linkedStream.lock()) // check pointer validity
  {
    ISampleReader* linkStreamReader = linkStream->GetReader();
    if (!linkStreamReader)
    {
      LOG::LogF(LOGERROR, "Cannot unlink stream, due to missing sample reader on main stream");
      return;
    }

    if (linkStreamReader->GetType() == ISampleReader::Type::TS)
    {
      auto tsReader = static_cast<CTSSampleReader*>(linkStreamReader);
      tsReader->RemoveStreamType(m_info.GetStreamType());
    }
    else if (linkStreamReader->GetType() == ISampleReader::Type::FMP4)
    {
      m_streamReader.reset();
    }
    else
    {
      LOG::LogF(LOGERROR, "Cannot unlink stream, due to unsupported sample reader type: %i",
                linkStreamReader->GetType());
    }
  }

  m_linkedStream.reset();
}
bool SESSION::CStream::IsLinkedToStreamType(INPUTSTREAM_TYPE type) const
{
  if (auto linkStream = m_linkedStream.lock()) // check pointer validity
  {
    return linkStream->m_info.GetStreamType() == type;
  }
  return false;
}
