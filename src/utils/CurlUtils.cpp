/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CurlUtils.h"

#include "StringUtils.h"
#include "log.h"

using namespace UTILS::CURL;

UTILS::CURL::CUrl::CUrl(std::string_view url)
{
  if (m_file.CURLCreate(url.data()))
  {
    // Default curl options
    m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "seekable", "0");
    m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "acceptencoding", "gzip, deflate");
    m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "failonerror", "false");
  }
}

UTILS::CURL::CUrl::~CUrl()
{
  m_file.Close();
}

int UTILS::CURL::CUrl::Open(bool isMediaStream /* = false */)
{
  unsigned int flags = ADDON_READ_NO_CACHE | ADDON_READ_CHUNKED;
  if (isMediaStream)
    flags |= ADDON_READ_AUDIO_VIDEO;

  if (!m_file.CURLOpen(flags))
  {
    LOG::LogF(LOGERROR, "CURLOpen failed");
    return -1;
  }

  // Get HTTP response status line (e.g. "HTTP/1.1 200 OK")
  std::string statusLine = m_file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_PROTOCOL, "");
  if (!statusLine.empty())
    return STRING::ToInt32(statusLine.substr(statusLine.find(' ') + 1), -1);

  return -1;
}

void UTILS::CURL::CUrl::AddHeader(std::string_view name, std::string_view value)
{
  m_file.CURLAddOption(ADDON_CURL_OPTION_HEADER, name.data(), value.data());
}

void UTILS::CURL::CUrl::AddHeaders(const std::map<std::string, std::string>& headers)
{
  for (auto& header : headers)
  {
    m_file.CURLAddOption(ADDON_CURL_OPTION_HEADER, header.first, header.second);
  }
}

std::string UTILS::CURL::CUrl::GetResponseHeader(std::string_view name)
{
  return m_file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, name.data());
}

std::string UTILS::CURL::CUrl::GetEffectiveUrl()
{
  return m_file.GetPropertyValue(ADDON_FILE_PROPERTY_EFFECTIVE_URL, "");
}

ReadStatus UTILS::CURL::CUrl::Read(std::string& data, size_t chunkBufferSize /* = BUFFER_SIZE_32 */)
{
  while (true)
  {
    std::vector<char> bufferData(chunkBufferSize);
    ssize_t ret = m_file.Read(bufferData.data(), chunkBufferSize);

    if (ret == -1)
      return ReadStatus::ERROR;
    else if (ret == 0)
      return ReadStatus::IS_EOF;

    data.append(bufferData.data(), static_cast<size_t>(ret));
    m_bytesRead += static_cast<size_t>(ret);
  }
}

ReadStatus UTILS::CURL::CUrl::ReadChunk(void* buffer, size_t bufferSize, size_t& bytesRead)
{
  ssize_t ret = m_file.Read(buffer, bufferSize);
  if (ret == -1)
    return ReadStatus::ERROR;
  else if (ret == 0)
    return ReadStatus::IS_EOF;

  bytesRead = static_cast<size_t>(ret);
  m_bytesRead += static_cast<size_t>(ret);
  return ReadStatus::CHUNK_READ;
}

double UTILS::CURL::CUrl::GetDownloadSpeed()
{
  return m_file.GetFileDownloadSpeed();
}

bool UTILS::CURL::CUrl::IsChunked()
{
  std::string transferEncodingStr{
      m_file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, "Transfer-Encoding")};
  std::string contentLengthStr{
      m_file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, "Content-Length")};
  // for HTTP2 connections are always 'chunked', so we use the absence of content-length
  // to flag this (also implies chunked with HTTP1)
  return contentLengthStr.empty() || transferEncodingStr.find("hunked") != std::string::npos;
}

bool UTILS::CURL::CUrl::IsEOF()
{
  return m_file.AtEnd();
}

bool UTILS::CURL::DownloadFile(std::string_view url,
                               const std::map<std::string, std::string>& reqHeaders,
                               const std::vector<std::string>& respHeaders,
                               HTTPResponse& resp)
{
  if (url.empty())
    return false;

  CURL::CUrl curl{url};
  curl.AddHeaders(reqHeaders);

  int statusCode = curl.Open();

  if (statusCode == -1)
    LOG::Log(LOGERROR, "Download failed, internal error: %s", url.data());
  else if (statusCode >= 400)
    LOG::Log(LOGERROR, "Download failed, HTTP error %d: %s", statusCode, url.data());
  else // Start the download
  {
    resp.effectiveUrl = curl.GetEffectiveUrl();

    if (curl.Read(resp.data) != CURL::ReadStatus::IS_EOF)
    {
      LOG::Log(LOGERROR, "Download failed: %s", statusCode, url.data());
      return false;
    }

    if (resp.data.empty())
    {
      LOG::Log(LOGERROR, "Download failed, no data: %s", url.data());
      return false;
    }

    resp.headers["content-type"] = curl.GetResponseHeader("content-type");
    for (std::string_view name : respHeaders)
    {
      resp.headers[name.data()] = curl.GetResponseHeader(name);
    }

    resp.downloadSpeed = curl.GetDownloadSpeed();
    resp.dataSize = curl.GetTotalByteRead();

    LOG::Log(LOGDEBUG, "Download finished: %s (downloaded %zu byte, speed %0.2lf byte/s)",
             url.data(), curl.GetTotalByteRead(), resp.downloadSpeed);
    return true;
  }
  return false;
}
