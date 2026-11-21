/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SegmentBuffer.h"

#include "utils/log.h"

#include <cstring>

using namespace ADP;

void ADP::SegmentBuffer::ChangeState(BufferState state)
{
  std::lock_guard<std::mutex> lock(m_rwMutex);
  m_state = state;
}

BufferState ADP::SegmentBuffer::State() const
{
  std::lock_guard<std::mutex> lock(m_rwMutex);
  return m_state;
}

ADP::SegmentBuffer::Snapshot ADP::SegmentBuffer::GetSnapshot() const
{
  std::lock_guard<std::mutex> lock(m_rwMutex);
  return {m_state, buffer.size()};
}

const std::vector<uint8_t> ADP::SegmentBuffer::ReadBuffer() const
{
  std::lock_guard<std::mutex> lock(m_rwMutex);
  return buffer;
}

void ADP::SegmentBuffer::CopyBufferTo(void* dest, size_t offset, size_t size)
{
  std::lock_guard<std::mutex> lock(m_rwMutex);

  if (!dest)
  {
    LOG::LogF(LOGERROR, "Destination pointer is null");
    return;
  }

  if (size == 0) // Nothing to copy
    return;

  if (offset > buffer.size())
  {
    LOG::LogF(LOGERROR, "Data offset is out of bounds");
    return;
  }

  if (offset + size > buffer.size())
  {
    LOG::LogF(LOGERROR, "Requested data size exceeds buffer bounds");
    return;
  }

  std::memcpy(dest, buffer.data() + offset, size);
}

void ADP::SegmentBuffer::AppendBuffer(const std::vector<uint8_t>& data)
{
  std::lock_guard<std::mutex> lock(m_rwMutex);
  buffer.insert(buffer.end(), data.begin(), data.end());
}

size_t ADP::SegmentBuffer::BufferSize() const
{
  std::lock_guard<std::mutex> lock(m_rwMutex);
  return buffer.size();
}

void ADP::CSegmentBuffers::Push(SegmentBuffer&& segBuffer)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  segBuffer.ChangeState(BufferState::QUEUED);

  m_buffers.emplace_back(std::move(segBuffer));

  m_cvWaitSegment.notify_one(); // Notify "wait for segment" condition
}

SegmentBuffer& ADP::CSegmentBuffers::Front()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_buffers.front();
}

SegmentBuffer& ADP::CSegmentBuffers::FrontSeg()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it)
  {
    if (!it->segment.IsInitialization())
      return *it;
  }
  LOG::LogF(LOGERROR, "Cannot find a segment, initialization segments only");
  return m_buffers.front();
}

SegmentBuffer& ADP::CSegmentBuffers::Back()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_buffers.back();
}

SegmentBuffer& ADP::CSegmentBuffers::BackSeg()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto it = m_buffers.rbegin(); it != m_buffers.rend(); ++it)
  {
    if (!it->segment.IsInitialization())
      return *it;
  }
  LOG::LogF(LOGERROR, "Cannot find a segment, initialization segments only");
  return m_buffers.back();
}

bool ADP::CSegmentBuffers::PopFront()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_buffers.empty())
    return false;

  m_buffers.pop_front();

  if (m_downloadIndex > 0)
    --m_downloadIndex;

  return true;
}

void ADP::CSegmentBuffers::SetMaxSize(size_t size)
{
  m_maxSize = size;
}

const size_t ADP::CSegmentBuffers::GetSize() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_buffers.size();
}

const size_t ADP::CSegmentBuffers::GetSizeDownloaded() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  size_t size{0};
  for (const auto& segBuffer : m_buffers)
  {
    if (segBuffer.State() == BufferState::DOWNLOADED ||
        segBuffer.State() == BufferState::DOWNLOADING)
      size++;
    else
      break;
  }
  return size;
}

const bool ADP::CSegmentBuffers::IsEmpty() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_buffers.empty();
}

bool ADP::CSegmentBuffers::IsBufferFull() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_buffers.size() >= m_maxSize;
}

void ADP::CSegmentBuffers::WaitForSegment()
{
  std::unique_lock<std::mutex> lock(m_mutex);
  m_cvWaitSegment.wait(lock,
                       [this] { return m_downloadIndex < m_buffers.size() || m_forceUnlock; });
  m_forceUnlock = false;
}

SegmentBuffer* ADP::CSegmentBuffers::GetNextDownload()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_buffers.empty())
    return nullptr;

  if (m_buffers.size() <= m_downloadIndex)
  {
    return nullptr;
  }

  return &m_buffers.at(m_downloadIndex);
}

void ADP::CSegmentBuffers::NotifyDownloadCompleted()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_buffers.size() < m_downloadIndex)
  {
    LOG::LogF(LOGERROR, "Cannot find the buffer segment at index %zu", m_downloadIndex);
    return;
  }
  // Increase the index to process the next download (if any)
  ++m_downloadIndex;
}

void ADP::CSegmentBuffers::Reset()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_buffers.clear();
  m_downloadIndex = 0;
  // Unlock "WaitForSegment" method
  m_forceUnlock = true;
  m_cvWaitSegment.notify_all();
}

bool ADP::CSegmentBuffers::ContainsSegment(const PLAYLIST::CSegment& seg) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  for (const auto& segBuffer : m_buffers)
  {
    if (segBuffer.segment.IsSame(seg))
      return true;
  }
  return false;
}
