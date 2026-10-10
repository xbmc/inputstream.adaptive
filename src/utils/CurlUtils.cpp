/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CurlUtils.h"

#include "Base64Utils.h"
#include "StringUtils.h"
#include "UrlUtils.h"
#include "Utils.h"
#include "log.h"

#include "CompResources.h"
#include "CompKodiProps.h"
#include "SrvBroker.h"

#include <algorithm>
#include <limits>

using namespace UTILS;
using namespace UTILS::CURL;

namespace
{
//! @todo: Cookies management here its an hack, currently there are no ways to have a persistent
//! HTTP session with Kodi binary interface, at least not in the usual way.
//! Curl library implementation in kodi manage a pool of connections (sessions) by domain,
//! and this actually keeps the cookies in memory but not in a persistent way,
//! when a connection remain unused for some seconds will be deleted and so delete cookies,
//! or when there is a multithreading access could pick a session (easy_handle) currently busy,
//! therefore it create a new empty session (easy_handle) ofc with no cookies,
//! because they are stored on another curl session that could be meantime deleted, see:
//! https://github.com/xbmc/xbmc/blob/21.0a3-Omega/xbmc/filesystem/DllLibCurl.cpp#L143-L146
//! https://github.com/xbmc/xbmc/blob/21.0a3-Omega/xbmc/filesystem/DllLibCurl.cpp#L173
//! A solution could be the use of curl shared data CURLOPT_SHARE to share data with multiple handles,
//! but there is still a need to review how to implement this in the binary addons interface as well,
//! or another solution could be add and implement the curl library dependency in to ISA itself.

std::unordered_set<Cookie> ParseCookies(const std::string& url,
                                        const std::vector<std::string>& cookies)
{
  // Be aware that kodi::vfs::GetCookies output dont provide all cookie attributes.
  std::unordered_set<Cookie> cookieList;

  // example: __Secure-NAME_EXAMPLE=VALUE; path=/; domain=.example.com
  for (const std::string& cookieStr : cookies)
  {
    Cookie cookie;
    const std::vector<std::string> params = STRING::SplitToVec(cookieStr, ';');
    for (const std::string& param : params)
    {
      std::string name;
      std::string value;
      const size_t sepPos = param.find('=');
      if (sepPos != std::string::npos)
      {
        name = STRING::ToLower(STRING::Trim(param.substr(0, sepPos)));
        value = STRING::Trim(param.substr(sepPos + 1));
      }
      else
        name = STRING::ToLower(STRING::Trim(param));

      if (cookie.m_name.empty()) // First param cookie name/value
      {
        cookie.m_name = name;
        cookie.m_value = value;
      }
      else if (name == "path")
      {
        cookie.m_path = value;
      }
      else if (name == "domain" && !value.empty())
      {
        if (value.front() == '*')
          value.erase(0, 1);
        cookie.m_domain = STRING::ToLower(value);
      }
      else if (name == "max-age") // max-age value has the precedence over "Expires"
      {
        cookie.m_expires = UTILS::GetTimestamp() + STRING::ToUint64(value);
      }
      else if (name == "expires" && cookie.m_expires == 0)
      {
        cookie.m_expires = UTILS::ConvertDate2822ToTs(value);
      }
    }

    if (cookie.m_domain.empty())
    {
      // If empty retrieve the hostname from the url (www.example.com)
      cookie.m_domain = URL::GetBaseDomain(url);
      const size_t protStartPos = cookie.m_domain.find("://");
      if (protStartPos != std::string::npos)
        cookie.m_domain.erase(0, protStartPos + 3);
    }

    if (cookie.m_path.empty())
    {
      // When empty fallback to current url path
      cookie.m_path = URL::GetPath(url, true);
    }

    cookieList.emplace(cookie);
  }
  return cookieList;
}

std::string GetCookies(const std::string& url)
{
  auto& srvResources = CSrvBroker::GetResources();
  std::lock_guard<std::mutex> lock(srvResources.GetCookiesMutex());

  // Get hostname (www.example.com)
  std::string hostname = URL::GetBaseDomain(url);
  const size_t protStartPos = hostname.find("://");
  if (protStartPos != std::string::npos)
    hostname.erase(0, protStartPos + 3);

  // Get domain, following format: .example.com
  std::string domain = hostname;
  const size_t dotPos = domain.find('.');
  if (dotPos != std::string::npos)
    domain.erase(0, dotPos);

  std::string urlPath = URL::GetPath(url, true);

  std::string cookiesStr;
  std::unordered_set<Cookie>& cookies = srvResources.Cookies();
  uint64_t currentTimestamp = UTILS::GetTimestamp();

  for (const Cookie& cookie : cookies)
  {
    // Check domain
    if (STRING::Contains(cookie.m_domain, domain) || STRING::Contains(cookie.m_domain, hostname))
    {
      // Check path, take in account directory and subdirectories
      if (!cookie.m_path.empty() && cookie.m_path != "/")
      {
        if (!STRING::StartsWith(urlPath, cookie.m_path))
          continue;
        // else if cookie path is like a file path e.g. "/name" allow subdirectories only
        else if (cookie.m_path.back() != '/' && urlPath.size() > cookie.m_path.size() &&
                 urlPath[cookie.m_path.size()] != '/')
          continue;
      }

      // Check expiry time
      if (cookie.m_expires <= currentTimestamp)
        continue;

      cookiesStr += cookie.m_name + "=" + cookie.m_value + ";";
    }
  }
  return cookiesStr;
}

void StoreCookies(const std::string& url, const std::vector<std::string>& cookiesStr)
{
  auto& srvResources = CSrvBroker::GetResources();
  std::lock_guard<std::mutex> lock(srvResources.GetCookiesMutex());
  std::unordered_set<Cookie>& cookies = srvResources.Cookies();

  // Delete existing expired cookies
  uint64_t currentTimestamp = UTILS::GetTimestamp();
  for (auto itCookie = cookies.begin(); itCookie != cookies.end();)
  {
    if (itCookie->m_expires > currentTimestamp)
      itCookie = cookies.erase(itCookie);
    else
      itCookie++;
  }

  if (cookiesStr.empty())
    return;

  std::unordered_set<Cookie> cookiesParsed = ParseCookies(url, cookiesStr);
  for (const auto& c : cookiesParsed)
  {
    if (cookies.find(c) != cookies.end())
      cookies.erase(c); // Delete existing cookie to update it

    cookies.insert(c);
  }
}
} // unnamed namespace

UTILS::CURL::CUrl::CUrl(const std::string& url, const RequestType reqType /* = RequestType::AUTO */)
  : m_url(url), m_isHttp(URL::IsHttpUrl(url))
{
  if (!m_isHttp)
    return;

  if (m_file.CURLCreate(url))
  {
    auto& kodiProps = CSrvBroker::GetKodiProps();

    if (reqType == RequestType::HEAD)
      m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "customrequest", "HEAD");

    // Default curl options
    m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "seekable", "0");

    if (!kodiProps.GetConfig().curlDisableAcceptEncoding)
      m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "acceptencoding", "all"); // Accept all encodings, e.g. gzip, deflate, br

    m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "failonerror", "false");
    if (!kodiProps.GetConfig().curlSSLVerifyPeer)
      m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "verifypeer", "false");

    AddHeaders(kodiProps.GetCommonHeaders());

    // Add session cookies
    // NOTE: if kodi property inputstream.adaptive.stream_headers is set with "cookie" header
    // the cookies set by the property will replace these
    if (kodiProps.GetConfig().internalCookies)
      m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "cookie", GetCookies(url));
  }
}

UTILS::CURL::CUrl::CUrl(const std::string& url, const std::string& postData) : CUrl::CUrl(url)
{
  if (m_file.IsOpen() && !postData.empty())
  {
    m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "postdata", BASE64::Encode(postData));
  }
}

UTILS::CURL::CUrl::~CUrl()
{
  if (m_isHttp && CSrvBroker::GetKodiProps().GetConfig().internalCookies)
    StoreCookies(GetEffectiveUrl(), GetResponseHeaders("set-cookie"));

  m_file.Close();
}

int UTILS::CURL::CUrl::Open()
{
  if (m_isHttp)
  {
    if (!m_file.CURLOpen(ADDON_READ_NO_CACHE | ADDON_READ_NO_BUFFER))
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

  const uint64_t maxOffset{static_cast<uint64_t>(std::numeric_limits<int64_t>::max())};
  if ((m_rangeBegin && *m_rangeBegin > maxOffset) || (m_rangeEnd && *m_rangeEnd > maxOffset) ||
      (m_rangeBegin && m_rangeEnd && *m_rangeEnd < *m_rangeBegin))
  {
    LOG::Log(LOGERROR, "Cannot open VFS resource, invalid byte range: %s", m_url.c_str());
    return -1;
  }

  if (!m_file.OpenFile(m_url, ADDON_READ_NO_CACHE | ADDON_READ_NO_BUFFER))
  {
    LOG::Log(LOGERROR, "Cannot open VFS resource: %s", m_url.c_str());
    return -1;
  }

  if (m_rangeBegin && *m_rangeBegin > 0 &&
      m_file.Seek(static_cast<int64_t>(*m_rangeBegin), SEEK_SET) !=
          static_cast<int64_t>(*m_rangeBegin))
  {
    LOG::Log(LOGERROR, "Cannot seek VFS resource: %s", m_url.c_str());
    m_file.Close();
    return -1;
  }

  m_bytesRead = 0;
  m_bytesRemaining.reset();
  m_reachedEof = false;
  if (m_rangeBegin && m_rangeEnd)
    m_bytesRemaining = *m_rangeEnd - *m_rangeBegin + 1;
  m_fileLength = m_file.GetLength();
  return 200;
}

void UTILS::CURL::CUrl::AddHeader(const std::string& name, const std::string& value)
{
  if (m_isHttp)
    m_file.CURLAddOption(ADDON_CURL_OPTION_HEADER, name, value);
}

void UTILS::CURL::CUrl::AddHeaders(const std::map<std::string, std::string>& headers)
{
  for (auto& header : headers)
    AddHeader(header.first, header.second);
}

void UTILS::CURL::CUrl::SetByteRange(uint64_t begin, std::optional<uint64_t> end)
{
  if (m_isHttp)
  {
    std::string value{"bytes=" + std::to_string(begin) + "-"};
    if (end)
      value += std::to_string(*end);
    AddHeader("Range", value);
    return;
  }

  m_rangeBegin = begin;
  m_rangeEnd = end;
}

std::string UTILS::CURL::CUrl::GetResponseHeader(const std::string& name)
{
  return m_isHttp ? m_file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, name) : "";
}

std::vector<std::string> UTILS::CURL::CUrl::GetResponseHeaders(const std::string& name)
{
  return m_isHttp ? m_file.GetPropertyValues(ADDON_FILE_PROPERTY_RESPONSE_HEADER, name)
                  : std::vector<std::string>{};
}

std::string UTILS::CURL::CUrl::GetEffectiveUrl()
{
  return m_isHttp ? m_file.GetPropertyValue(ADDON_FILE_PROPERTY_EFFECTIVE_URL, "") : m_url;
}

ReadStatus UTILS::CURL::CUrl::Read(std::string& data, size_t chunkBufferSize /* = BUFFER_SIZE_32 */)
{
  while (true)
  {
    std::vector<char> bufferData(chunkBufferSize);
    size_t bytesRead{0};
    const ReadStatus status = ReadChunk(bufferData.data(), chunkBufferSize, bytesRead);
    if (status != ReadStatus::CHUNK_READ)
      return status;
    data.append(bufferData.data(), bytesRead);
  }
}

ReadStatus UTILS::CURL::CUrl::ReadChunk(void* buffer, size_t bufferSize, size_t& bytesRead)
{
  bytesRead = 0;
  if (m_bytesRemaining && *m_bytesRemaining == 0)
    return ReadStatus::IS_EOF;

  size_t readSize{bufferSize};
  if (m_bytesRemaining)
    readSize = static_cast<size_t>(std::min<uint64_t>(*m_bytesRemaining, bufferSize));

  ssize_t ret = m_file.Read(buffer, readSize);
  if (ret == -1)
    return ReadStatus::ERROR;
  else if (ret == 0)
  {
    m_reachedEof = true;
    return ReadStatus::IS_EOF;
  }

  bytesRead = static_cast<size_t>(ret);
  m_bytesRead += static_cast<size_t>(ret);
  if (m_bytesRemaining)
    *m_bytesRemaining -= bytesRead;
  return ReadStatus::CHUNK_READ;
}

double UTILS::CURL::CUrl::GetDownloadSpeed()
{
  return m_isHttp ? m_file.GetFileDownloadSpeed() : 0.0;
}

bool UTILS::CURL::CUrl::IsChunked()
{
  if (!m_isHttp)
    return false;

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
  if (m_bytesRemaining && *m_bytesRemaining == 0)
    return true;
  if (!m_isHttp)
  {
    if (m_fileLength >= 0)
      return m_file.GetPosition() >= m_fileLength;
    return m_reachedEof;
  }
  return m_file.AtEnd();
}

bool UTILS::CURL::DownloadFile(const std::string& url,
                               const std::map<std::string, std::string>& reqHeaders,
                               const std::vector<std::string>& respHeaders,
                               HTTPResponse& resp)
{
  if (url.empty())
    return false;

  size_t retries{3};

  while (retries-- > 0)
  {
    CURL::CUrl curl{url};
    curl.AddHeaders(reqHeaders);

    int statusCode = curl.Open();

    if (statusCode == -1)
    {
      LOG::Log(LOGERROR, "Download failed, internal error: %s", url.c_str());
      break;
    }
    else if (statusCode >= 500)
    {
      continue; // Try again
    }
    else if (statusCode >= 400)
    {
      LOG::Log(LOGERROR, "Download failed, HTTP error %d: %s", statusCode, url.c_str());
      break;
    }
    else // Start the download
    {
      resp.effectiveUrl = curl.GetEffectiveUrl();

      if (curl.Read(resp.data) != CURL::ReadStatus::IS_EOF)
      {
        LOG::Log(LOGERROR, "Download failed: %s", statusCode, url.c_str());
        break;
      }

      if (resp.data.empty())
      {
        LOG::Log(LOGERROR, "Download failed, no data: %s", url.c_str());
        break;
      }

      resp.headers["content-type"] = curl.GetResponseHeader("content-type");
      for (const std::string& name : respHeaders)
      {
        resp.headers[name.c_str()] = curl.GetResponseHeader(name);
      }

      resp.downloadSpeed = curl.GetDownloadSpeed();
      resp.dataSize = curl.GetTotalByteRead();

      LOG::Log(LOGDEBUG, "Download finished: %s (downloaded %zu byte, speed %0.2lf byte/s)",
               url.c_str(), curl.GetTotalByteRead(), resp.downloadSpeed);
      return true;
    }
  }

  return false;
}
