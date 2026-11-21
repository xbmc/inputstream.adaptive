/*
 *  Copyright (C) 2025 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "Segment.h"
#include "Representation.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

namespace ADP
{
enum class BufferState
{
  NONE, // No status
  QUEUED, // Queued for the download
  DOWNLOADING, // Download in progress
  DOWNLOADED, // Download completed
  INVALID, // Not valid data (e.g. download error)
};

struct SegmentBuffer
{
  struct Snapshot
  {
    BufferState state;
    size_t size;
  };

  SegmentBuffer() = default;
  ~SegmentBuffer() = default;

  void ChangeState(BufferState state);
  BufferState State() const;
  Snapshot GetSnapshot() const;

  PLAYLIST::CSegment segment;
  PLAYLIST::CRepresentation* rep{nullptr};

  const std::vector<uint8_t> ReadBuffer() const;

  void CopyBufferTo(void* dest, size_t offset, size_t size);

  void AppendBuffer(const std::vector<uint8_t>& data);

  size_t BufferSize() const;

  SegmentBuffer& operator=(const SegmentBuffer&) = delete;

  // Copy constructor
  SegmentBuffer(const SegmentBuffer& other)
    : m_state(other.m_state), segment(other.segment), rep(other.rep), buffer(other.ReadBuffer())
  {
    // Do not copy mutex
  }

  // Move constructor
  SegmentBuffer(SegmentBuffer&& other) noexcept
    : m_state(other.m_state),
      segment(std::move(other.segment)),
      rep(other.rep),
      buffer(std::move(other.buffer))
  {
    // Do not copy mutex
  }

private:
  mutable std::mutex m_rwMutex;
  std::vector<uint8_t> buffer;
  BufferState m_state{BufferState::NONE};
};

/*
 * \brief Segment buffer management,
 *        Note that all data related to segments stored in SegmentBuffer objects are static,
 *        these data are totally unrelated to manifest updates that may change over the time. 
 */
class ATTR_DLL_LOCAL CSegmentBuffers
{
public:
  CSegmentBuffers() = default;
  ~CSegmentBuffers() = default;

  void Push(SegmentBuffer&& segBuffer);

 /*!
  * \brief Returns a reference to the first segment in the buffers container.
  *        The behavior is undefined when there are no segments.
  * \return The first element in the segment buffers
  */
  SegmentBuffer& Front();

 /*!
  * \brief Returns a reference to the first segment in the buffers container
  *        that is not an initialization segment.
  *        The behavior is undefined when there are no segments.
  * \return The first non-initialization element in the segment buffers
  */
  SegmentBuffer& FrontSeg();

 /*!
  * \brief Returns a reference to the last segment in the buffers container.
  *        The behavior is undefined when there are no segments.
  * \return The last element in the segment buffers
  */
  SegmentBuffer& Back();

 /*!
  * \brief Returns a reference to the last segment in the buffers container
  *        that is not an initialization segment.
  *        The behavior is undefined when there are no segments.
  * \return The last non-initialization element in the segment buffers
  */
  SegmentBuffer& BackSeg();

 /*!
  * \brief Removes the first element of the segment buffers container.
  * \return The true if a element has been removed, otherwise false.
  */
  bool PopFront();

 /*!
  * \brief Sets the maximum size of elements that the buffer can contain.
  * \param size The size of buffer.
  */
  void SetMaxSize(size_t size);

 /*!
  * \brief Get the current number of elements contained in the segment buffers container.
  */
  const size_t GetSize() const;

  /*!
  * \brief Get the current number of elements contained in the segment buffers container that are in download or downloaded.
  */
  const size_t GetSizeDownloaded() const;

 /*!
  * \brief Determines whether the segment buffers container is empty.
  * \return True if empty, otherwise false.
  */
  const bool IsEmpty() const;

 /*!
  * \brief Determines whether the buffer is full, has reached its maximum size.
  * \return True if full, otherwise false.
  */
  bool IsBufferFull() const;

 /*!
  * \brief Blocks code execution whenever there is no segment to download.
  *        This behavior will be unlocked when a new segment will be inserted,
  *        so by the "PopFront" method or "Reset" method.
  */
  void WaitForSegment();

 /*!
  * \brief Returns a pointer to the first segment that needs to be downloaded.
  *        When the download is completed must be followed by a callback to NotifyDownloadCompleted method,
  *        in order to notify that the process to download the segment has been completed.
  * \return The first element that needs to be downloaded.
  */
  SegmentBuffer* GetNextDownload();

 /*!
  * \brief Notifies that current buffer in download has been processed with specified state.
  */
  void NotifyDownloadCompleted();

 /*!
  * \brief Reset the segment buffers, clear segments and internal statuses.
  */
  void Reset();

 /*!
  * \brief Determines whether the specified segment is contained in the buffer, regardless of its download status.
  * \return True if segment exists, otherwise false.
  */
  bool ContainsSegment(const PLAYLIST::CSegment& seg) const;

private:
  mutable std::mutex m_mutex;
  std::condition_variable m_cvWaitSegment;
  std::deque<SegmentBuffer> m_buffers;
  size_t m_maxSize{10};
  size_t m_downloadIndex{0};
  bool m_forceUnlock{false};
};

} // namespace ADP
