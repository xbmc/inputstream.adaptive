/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <algorithm>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

// forwards
class AP4_Movie;
namespace adaptive
{
class AdaptiveStream;
}
namespace kodi::addon
{
class InputstreamInfo;
}

namespace PLAYLIST
{
// Marker for the default psshset position
constexpr uint16_t PSSHSET_POS_DEFAULT = 0;
// Marker for not valid psshset position
constexpr uint16_t PSSHSET_POS_INVALID = std::numeric_limits<uint16_t>::max();
// Marker for not set/not found segment position
constexpr size_t SEGMENT_NO_POS = std::numeric_limits<size_t>::max();
// Marker for not set/not found segment number
constexpr uint64_t SEGMENT_NO_NUMBER = std::numeric_limits<uint64_t>::max();
// Marker for undefined timestamp value
constexpr uint64_t NO_PTS_VALUE = std::numeric_limits<uint64_t>::max();
// Marker for undefined value
constexpr uint64_t NO_VALUE = std::numeric_limits<uint64_t>::max();

// Kodi VideoPlayer internal buffer
constexpr uint64_t KODI_VP_BUFFER_SECS = 8;

enum class EncryptionState
{
  UNENCRYPTED,
  ENCRYPTED_DRM, // DRM encrypted
  ENCRYPTED_CK, // ClearKey encrypted (e.g. AES-128)
  NOT_SUPPORTED, // Unsupported encryption
};

enum class EncryptionType
{
  NOT_SUPPORTED,
  NONE,
  CLEARKEY,
  AES128,
  WIDEVINE,
};

enum class ContainerType
{
  NOTYPE,
  INVALID,
  MP4,
  TS,
  ADTS,
  WEBM,
  MATROSKA,
  TEXT,
};

enum class StreamType
{
  NOTYPE,
  VIDEO,
  AUDIO,
  SUBTITLE,
  VIDEO_AUDIO,
};

/*!
 * \brief Convert StreamType enum value into a human readable string.
 * \param streamType The stream type to convert
 * \return Human readable StreamType string
 */
std::string_view StreamTypeToString(const StreamType streamType);

/*!
 * \brief Parse a range string, as RFC 7233 (e.g. for DASH).
 * \param range The range string to parse of format like "n-n"
 * \param start [OUT] The start position
 * \param end [OUT] The end position
 * \return True when parsed correctly, otherwise false
 */
bool ParseRangeRFC(std::string_view range, uint64_t& start, uint64_t& end);

/*!
 * \brief Parse range values, the second value splitted by a separator char is optional.
 * \param range The range string to parse of format, e.g. "n" or "n[separator]n"
 * \param first [OUT] The first value, if not parsed nothing will be written
 * \param second [OUT] The second optional value, if not parsed nothing will be written
 * \return True when parsed correctly, otherwise false
 */
bool ParseRangeValues(std::string_view range,
                      uint64_t& first,
                      uint64_t& second,
                      char separator = '@');

/*!
 * \brief Create a Movie atom based on stream properties and info.
 * \param adStream The adaptive stream
 * \param streamInfo The stream info
 * \return The Movie atom
 */
AP4_Movie* CreateMovieAtom(adaptive::AdaptiveStream& adStream,
                           kodi::addon::InputstreamInfo& streamInfo);

// \brief Get the position of a pointer within a vector of unique_ptr
template<typename T, typename Ptr>
size_t GetPtrPosition(const std::vector<std::unique_ptr<T>>& container, const Ptr* ptr)
{
  auto itRepr = std::find_if(container.begin(), container.end(),
                             [&ptr](const std::unique_ptr<T>& r) { return r.get() == ptr; });
  return itRepr - container.begin();
}

} // namespace adaptive
