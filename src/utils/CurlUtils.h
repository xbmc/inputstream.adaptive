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

constexpr size_t BUFFER_SIZE_32 = 32 * 1024; // 32 Kbyte

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
  * \brief Get the last used url (after following redirects).
  * \return The url
  */
  std::string GetEffectiveUrl();

 /*!
  * \brief Download a file.
  * \param data[OUT] Where to write the downloaded data
  * \param chunkBufferSize[OPT] The buffer size for read chunks, default 32 kbyte
  * \return The read status
  */
  ReadStatus Read(std::string& data, size_t chunkBufferSize = BUFFER_SIZE_32);

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

struct HTTPResponse
{
  std::string effectiveUrl; // The last used url (after following redirects)
  std::string data; // Response data
  size_t dataSize{0}; // Response data size in bytes
  std::map<std::string, std::string> headers; // Headers retrieved from response
  double downloadSpeed{0}; // Download speed in byte/s
};

 /*!
  * \brief Helper method to download a file.
  * \param url Url of the file to download
  * \param reqHeaders Headers to use for the HTTP request
  * \param respHeaders Headers to get from the HTTP response,
  *                    by default content-type header is always retrieved.
  * \param resp The data from the response
  * \return True if has success, otherwise false
  */
bool DownloadFile(std::string_view url,
                  const std::map<std::string, std::string>& reqHeaders,
                  const std::vector<std::string>& respHeaders,
                  HTTPResponse& resp);

} // namespace CURL
} // namespace UTILS
