/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#ifdef INPUTSTREAM_TEST_BUILD
#include "../test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
//! @Todo: forward CFile?
#include <kodi/Filesystem.h>
#endif

#include <map>
#include <string>
#include <string_view>

namespace UTILS
{
namespace CURL
{

enum class ReadStatus
{
  IS_EOF, // The end-of-file is reached
  CHUNK_READ,
  ERROR,
};

class ATTR_DLL_LOCAL CUrl
{
public:
 /*!
  * \brief Create CUrl.
  * \param url The url of the file to download
  */
  CUrl(std::string_view url);
  ~CUrl();

 /*!
  * \brief Open the url.
  * \param isMediaStream Set true if the download is a media stream (audio/video/subtitles)
  * \return Return HTTP status code, or -1 for any internal error
  */
  int Open(bool isMediaStream = false);

  void AddHeader(std::string_view name, std::string_view value);
  void AddHeaders(const std::map<std::string, std::string>& headers);

 /*!
  * \brief Get an header from the HTTP response.
  * \param name The header name
  * \return The header value, or empty if none
  */
  std::string GetResponseHeader(std::string_view name);

 /*!
  * \brief Download / read a chunk.
  * \param buffer[OUT] Buffer where to write the chunk data
  * \param bufferSize The buffer size
  * \param bytesRead[OUT] The chunk size read
  * \return The read status
  */
  ReadStatus ReadChunk(void* buffer, size_t bufferSize, size_t& bytesRead);

 /*!
  * \brief Get the download speed in byte/s. To be called at the end of download.
  */
  double GetDownloadSpeed();

 /*!
  * \brief Get the total byte read of the download (total of chunks size).
  */
  size_t GetTotalByteRead() { return m_bytesRead; }

 /*!
  * \brief Determines if the data to download is in chunks.
  */
  bool IsChunked();

 /*!
  * \brief Determines if the has reach the EOF.
  */
  bool IsEOF();

private:
  kodi::vfs::CFile m_file;
  size_t m_bytesRead{0};
};

} // namespace CURL
} // namespace UTILS
