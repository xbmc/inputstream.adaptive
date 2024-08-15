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

std::unordered_set<Cookie> ParseCookies(std::string_view url,
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
      else if (name == "max-age") //! @todo: to implement "Expires" attribute parsing, but max-age value has the precedence over "Expires"
      {
        cookie.m_expires = GetTimestamp() + (STRING::ToUint64(value) * 1000);
      }
    }

    if (cookie.m_domain.empty())
    {
      // If empty retrieve the hostname from the url (www.example.com)
      cookie.m_domain = URL::GetBaseDomain(url.data());
      const size_t protStartPos = cookie.m_domain.find("://");
      if (protStartPos != std::string::npos)
        cookie.m_domain.erase(0, protStartPos + 3);
    }

    if (cookie.m_path.empty())
    {
      // When empty fallback to current url path
      cookie.m_path = URL::GetPath(url.data(), true);
    }

    cookieList.emplace(cookie);
  }
  return cookieList;
}

std::string GetCookies(std::string_view url)
{
  auto& srvResources = CSrvBroker::GetResources();
  std::lock_guard<std::mutex> lock(srvResources.GetCookiesMutex());

  // Get hostname (www.example.com)
  std::string hostname = URL::GetBaseDomain(url.data());
  const size_t protStartPos = hostname.find("://");
  if (protStartPos != std::string::npos)
    hostname.erase(0, protStartPos + 3);

  // Get domain, following format: .example.com
  std::string domain = hostname;
  const size_t dotPos = domain.find('.');
  if (dotPos != std::string::npos)
    domain.erase(0, dotPos);

  std::string urlPath = URL::GetPath(url.data(), true);

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

void StoreCookies(std::string_view url, const std::vector<std::string>& cookiesStr)
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

UTILS::CURL::CUrl::CUrl(std::string_view url)
{
  if (m_file.CURLCreate(url.data()))
  {
    auto& kodiProps = CSrvBroker::GetKodiProps();

    // Default curl options
    m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "seekable", "0");
    m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "acceptencoding", "gzip, deflate");
    m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "failonerror", "false");
    if (!kodiProps.GetConfig().curlSSLVerifyPeer)
      m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "verifypeer", "false");

    // Add session cookies
    // NOTE: if kodi property inputstream.adaptive.stream_headers is set with "cookie" header
    // the cookies set by the property will replace these
    if (kodiProps.GetConfig().internalCookies)
      m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "cookie", GetCookies(url));
  }
}

UTILS::CURL::CUrl::CUrl(std::string_view url, const std::string& postData) : CUrl::CUrl(url)
{
  if (m_file.IsOpen() && !postData.empty())
  {
    m_file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "postdata", BASE64::Encode(postData));
  }
}

UTILS::CURL::CUrl::~CUrl()
{
  if (CSrvBroker::GetKodiProps().GetConfig().internalCookies)
    StoreCookies(GetEffectiveUrl(), GetResponseHeaders("set-cookie"));

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

std::vector<std::string> UTILS::CURL::CUrl::GetResponseHeaders(std::string_view name)
{
  return m_file.GetPropertyValues(ADDON_FILE_PROPERTY_RESPONSE_HEADER, name.data());
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

  size_t retries{3};

  while (retries-- > 0)
  {
    CURL::CUrl curl{url};
    curl.AddHeaders(reqHeaders);

    int statusCode = curl.Open();

    if (statusCode == -1)
    {
      LOG::Log(LOGERROR, "Download failed, internal error: %s", url.data());
      break;
    }
    else if (statusCode >= 500)
    {
      continue; // Try again
    }
    else if (statusCode >= 400)
    {
      LOG::Log(LOGERROR, "Download failed, HTTP error %d: %s", statusCode, url.data());
      break;
    }
    else // Start the download
    {
      resp.effectiveUrl = curl.GetEffectiveUrl();

      if (curl.Read(resp.data) != CURL::ReadStatus::IS_EOF)
      {
        LOG::Log(LOGERROR, "Download failed: %s", statusCode, url.data());
        break;
      }

      if (resp.data.empty())
      {
        LOG::Log(LOGERROR, "Download failed, no data: %s", url.data());
        break;
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
  }

  return false;
}
