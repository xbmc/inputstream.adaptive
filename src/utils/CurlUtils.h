/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#include <kodi/Filesystem.h>
#endif

#include <iostream>
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

 /*!
  * \brief Create CUrl for POST request, if the data are empty, GET will be performed.
  * \param url The request url
  * \param postData The data for the POST request
  */
  CUrl(std::string_view url, const std::string& postData);
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

  std::vector<std::string> GetResponseHeaders(std::string_view name);

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

struct Cookie
{
  std::string m_name;
  std::string m_value;
  std::string m_domain;
  std::string m_path;
  uint64_t m_expires{0}; // expire timestamp

  bool operator==(const Cookie& other) const
  {
    return m_name == other.m_name && m_domain == other.m_domain;
  }
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

// Embedded Cookie hash specialization
namespace std
{
template<>
struct hash<UTILS::CURL::Cookie>
{
  size_t operator()(const UTILS::CURL::Cookie& cookie) const
  {
    return hash<string>()(cookie.m_name + cookie.m_domain);
  }
};
} // namespace std
