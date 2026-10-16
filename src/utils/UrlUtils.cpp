/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "UrlUtils.h"

#include "Base64Utils.h"
#include "StringUtils.h"
#include "log.h"

#include <cctype>

using namespace UTILS;

namespace
{
constexpr std::string_view PREFIX_SINGLE_DOT{"./"};
constexpr std::string_view PREFIX_DOUBLE_DOT{"../"};

size_t GetSchemeSeparator(std::string_view url)
{
  if (url.empty() || !std::isalpha(static_cast<unsigned char>(url.front())))
    return std::string_view::npos;

  for (size_t pos = 1; pos < url.size(); ++pos)
  {
    const unsigned char value = static_cast<unsigned char>(url[pos]);
    if (url[pos] == ':')
      return pos;
    if (!std::isalnum(value) && url[pos] != '+' && url[pos] != '-' && url[pos] != '.')
      return std::string_view::npos;
  }
  return std::string_view::npos;
}

bool IsScheme(std::string_view url, std::string_view scheme)
{
  const size_t separator = GetSchemeSeparator(url);
  return separator == scheme.size() && STRING::CompareNoCase(url.substr(0, separator), scheme);
}

bool IsMagnetUrl(std::string_view url)
{
  return IsScheme(url, "magnet");
}

size_t GetPathStart(std::string_view url)
{
  const size_t schemeSeparator = GetSchemeSeparator(url);
  if (schemeSeparator == std::string_view::npos)
    return 0;

  if (IsMagnetUrl(url))
  {
    const size_t pathStart = url.find_first_not_of('/', schemeSeparator + 1);
    return pathStart == std::string_view::npos ? url.size() : pathStart;
  }

  if (url.substr(schemeSeparator + 1).starts_with("//"))
  {
    const size_t pathStart = url.find('/', schemeSeparator + 3);
    return pathStart == std::string_view::npos ? url.size() : pathStart;
  }
  return schemeSeparator + 1;
}

void RemoveFragment(std::string& url)
{
  const size_t fragmentPos = url.find('#');
  if (fragmentPos != std::string::npos)
    url.erase(fragmentPos);
}

std::string RemoveQuery(std::string& url)
{
  const size_t queryPos = url.find('?');
  if (queryPos == std::string::npos)
    return {};

  std::string query{url.substr(queryPos)};
  url.erase(queryPos);
  return query;
}

bool isUrl(std::string url,
           bool allowFragments,
           bool allowQueryParams,
           bool validateLenght,
           bool validateProtocol,
           bool requireProtocol,
           bool allowRelativeUrls)
{
  if (url.empty())
    return false;

  if (validateLenght && url.size() > 8000) // Minimum length recommended by RFC 9110, section 4.1
    return false;

  if (!allowFragments && url.find('#') != std::string::npos)
    return false;

  if (!allowQueryParams &&
      (url.find('?') != std::string::npos || url.find('&') != std::string::npos))
    return false;

  size_t paramPos = url.find('#');
  if (paramPos != std::string::npos)
    url.resize(paramPos);

  paramPos = url.find('?');
  if (paramPos != std::string::npos)
    url.resize(paramPos);

  paramPos = url.find("://");
  if (paramPos != std::string::npos)
  {
    if (validateProtocol)
    {
      std::string protocol{url.substr(0, paramPos)};
      if (protocol != "http" && protocol != "https")
        return false;
    }
    url = url.substr(paramPos + 3);
  }
  else if (requireProtocol)
  {
    return false;
  }
  else if (url.compare(0, 1, "/") == 0)
  {
    if (!allowRelativeUrls)
      return false;

    url = url.substr(1);
  }

  if (url.empty())
    return false;

  return true;
}

void RemovePrefixSingleDot(std::string& url)
{
  size_t pos{0};
  // Remove occurrences of "/./" preserving the separator
  while ((pos = url.find("/./")) != std::string::npos)
  {
    url.erase(pos, 2);
  }

  if (url.ends_with("/."))
    url.pop_back(); // Delete the dot and preserve the separator
}

void RemovePrefixDoubleDot(std::string& url)
{
  size_t pos{0};
  // Remove occurrences of "/../" preserving the separator
  while ((pos = url.find("/../")) != std::string::npos)
  {
    url.erase(pos, 3);
  }
}

/*
 * \brief Remove and resolve special dot's from the end of the url.
 *        e.g. "http://foo.bar/sub1/sub2/.././" will result "http://foo.bar/sub1/"
 */
std::string RemoveDotSegments(std::string url)
{
  if (url.size() < 2)
    return url;

  // Count amount of special prefixes with double dots on the right side
  size_t numSegsRemove{0};
  size_t currPos{0};
  size_t startPos{url.size() - 2};
  while ((currPos = url.rfind('/', startPos)) != std::string::npos)
  {
    // Stop to ignore "/../" from the start of string, e.g. ignored --> "../../something/../" <-- handled
    if (currPos == 0 || url.substr(currPos + 1, startPos - currPos + 1) != PREFIX_DOUBLE_DOT)
      break;
    startPos = currPos - 1;
    numSegsRemove++;
  }

  // Remove special prefixes
  RemovePrefixSingleDot(url);
  RemovePrefixDoubleDot(url);

  const size_t pathStart = GetPathStart(url);
  size_t pathRootEnd{0};
  if (GetSchemeSeparator(url) != std::string_view::npos)
    pathRootEnd = pathStart < url.size() && url[pathStart] == '/' ? pathStart + 1 : pathStart;
  else if (URL::IsUrlRelativeLevel(url))
    pathRootEnd = 3;

  // Remove segments from the end (if any)
  for (; numSegsRemove > 0; numSegsRemove--)
  {
    std::size_t lastSlashPos = url.find_last_of('/', url.size() - 2);
    if (lastSlashPos == std::string::npos || lastSlashPos + 1 < pathRootEnd)
      url.resize(pathRootEnd);
    else
      url.resize(lastSlashPos + 1);
  }

  return url;
}

} // unnamed namespace

bool UTILS::URL::IsValidUrl(const std::string& url)
{
  return isUrl(url, false, true, true, true, true, false);
}

bool UTILS::URL::IsHttpUrl(std::string_view url)
{
  const size_t separator = GetSchemeSeparator(url);
  return (IsScheme(url, "http") || IsScheme(url, "https")) &&
         url.substr(separator + 1).starts_with("//");
}

bool UTILS::URL::IsUrlAbsolute(std::string_view url)
{
  return GetSchemeSeparator(url) != std::string_view::npos;
}

bool UTILS::URL::IsUrlRelative(std::string_view url)
{
  return !IsUrlAbsolute(url);
}

bool UTILS::URL::IsUrlRelativeLevel(std::string_view url)
{
  return (url.compare(0, 3, PREFIX_DOUBLE_DOT) == 0);
}

std::string UTILS::URL::GetParameters(std::string& url) {
  size_t paramsPos = url.find('?');
  if (paramsPos != std::string::npos)
    return url.substr(paramsPos + 1);

  return "";
}

std::string UTILS::URL::RemoveParameters(std::string url)
{
  size_t paramsPos = url.find('?');
  if (paramsPos != std::string::npos)
    url.resize(paramsPos);

  return url;
}

std::string UTILS::URL::GetPath(std::string url, bool includeFilePart)
{
  if (url.empty())
    return url;

  size_t paramsPos = url.find('?');
  if (paramsPos != std::string::npos)
    url.resize(paramsPos);

  const size_t domainStartPos = url.find("://") + 3;
  const size_t slashPos = url.find_first_of('/', domainStartPos);
  if (slashPos != std::string::npos)
  {
    if (!includeFilePart && url.back() != '/')
    {
      // The part of the base url after last / is not a directory so will not be taken into account
      size_t slashPos = url.rfind("/");
      if (slashPos > domainStartPos)
        url.erase(slashPos + 1);
    }
    return url.substr(slashPos);
  }
  return "/";
}

std::string UTILS::URL::GetUrlPath(std::string url)
{
  if (url.empty())
    return url;

  const bool preserveQuery = IsMagnetUrl(url);
  RemoveFragment(url);
  std::string query = RemoveQuery(url);
  if (!preserveQuery)
    query.clear();

  const size_t schemeSeparator = GetSchemeSeparator(url);
  const size_t pathStart = GetPathStart(url);

  // The part of the base url after last / is not a directory so will not be taken into account
  if (!url.empty() && url.back() != '/')
  {
    const size_t slashPos = url.rfind('/');
    if (slashPos != std::string::npos && slashPos >= pathStart)
      url.erase(slashPos + 1);
    else if (schemeSeparator != std::string::npos)
      url.erase(pathStart);
    else
      url.clear();
  }

  return url + query;
}

void UTILS::URL::AppendParameters(std::string& url, std::string_view params)
{
  if (params.empty() || params.front() == '|')
    return;

  params.remove_prefix(params.front() == '&' || params.front() == '?' ? 1 : 0);
  size_t keyValDividerPos;
  while ((keyValDividerPos = params.find('=')) != std::string::npos)
  {
    size_t ampersandPos = params.find('&');
    std::string paramKey{params.substr(0, keyValDividerPos)};

    // if param key is not in url add it
    if (url.find('?' + paramKey + '=') == std::string::npos &&
        url.find('&' + paramKey + '=') == std::string::npos)
    {
      url += url.find_first_of('?') == std::string::npos ? '?' : '&';
      url += paramKey + '=';
      if (ampersandPos != std::string::npos)
        url += params.substr(keyValDividerPos + 1, ampersandPos - keyValDividerPos - 1);
      else
        url += params.substr(keyValDividerPos + 1);
    }

    if (ampersandPos != std::string::npos)
      params = params.substr(ampersandPos + 1);
    else
      break;
  }
}

std::string UTILS::URL::GetBaseDomain(std::string url)
{
  if (IsUrlAbsolute(url))
  {
    const size_t paramsPos = url.find('?');
    if (paramsPos != std::string::npos)
      url.erase(paramsPos);

    const size_t schemeEndPos = url.find("://");
    if (schemeEndPos == std::string::npos)
      return ""; // Not valid

    const size_t domainStartPos = schemeEndPos + 3;
    const size_t pathPos = url.find_first_of('/', domainStartPos);

    if (pathPos != std::string::npos)
      url.erase(pathPos); // remove from slash

    return url;
  }
  return "";
}

std::string UTILS::URL::Join(std::string baseUrl, std::string relativeUrl)
{
  if (baseUrl.empty())
    return relativeUrl;

  if (relativeUrl.empty())
    return baseUrl;

  if (IsUrlAbsolute(relativeUrl))
    return relativeUrl;

  const bool inheritBaseQuery = IsMagnetUrl(baseUrl);
  RemoveFragment(baseUrl);
  std::string baseQuery = RemoveQuery(baseUrl);
  if (!inheritBaseQuery)
    baseQuery.clear();

  if (relativeUrl == ".") // Ignore single dot
    relativeUrl.clear();

  // Sanitize for missing backslash
  if (relativeUrl == ".." || relativeUrl.ends_with("/.."))
    relativeUrl += "/";

  // The part of the base url after last / is not a directory so will not be taken into account
  if (!baseUrl.empty() && baseUrl.back() != '/')
  {
    const size_t pathStart = GetPathStart(baseUrl);
    const size_t slashPos = baseUrl.rfind('/');
    if (slashPos != std::string::npos && slashPos >= pathStart)
      baseUrl.erase(slashPos + 1);
    else if (GetSchemeSeparator(baseUrl) != std::string_view::npos)
      baseUrl.erase(pathStart);
    else
      baseUrl.clear();
  }

  if (!baseUrl.empty() && baseUrl.back() != '/')
    baseUrl += "/";

  bool skipRemovingSegs{true};

  // Check if relative to domain
  if (!relativeUrl.empty() && relativeUrl.front() == '/')
  {
    skipRemovingSegs = false;
    relativeUrl.erase(0, 1);
    std::string domain = GetBaseDomain(baseUrl);
    if (!domain.empty())
      baseUrl = domain + "/";
  }

  if (IsUrlRelativeLevel(relativeUrl))
  {
    // Remove segments from the end of base url,
    // based on the initial prefixes "../" on the relativeUrl url
    size_t currPos{0};
    size_t startPos{0};
    while ((currPos = relativeUrl.find("/", startPos)) != std::string::npos)
    {
      // Stop to ignore "/../" from the end of string, e.g. handled --> "../../something/../" <-- ignored
      if (relativeUrl.substr(startPos, currPos + 1 - startPos) != PREFIX_DOUBLE_DOT)
        break;
      startPos = currPos + 1;
    }

    if (skipRemovingSegs)
      baseUrl = RemoveDotSegments(baseUrl + relativeUrl.substr(0, startPos));

    relativeUrl.erase(0, startPos);
  }

  std::string result = RemoveDotSegments(baseUrl + relativeUrl);
  if (baseQuery.size() > 1)
  {
    const size_t fragmentPos = result.find('#');
    const size_t queryPos = result.find('?');
    if (queryPos != std::string::npos &&
        (fragmentPos == std::string::npos || queryPos < fragmentPos))
      baseQuery.front() = '&';
    result.insert(fragmentPos == std::string::npos ? result.size() : fragmentPos, baseQuery);
  }
  return result;
}

void UTILS::URL::EnsureEndingBackslash(std::string& url)
{
  if (!url.empty() && url.back() != '/')
    url += "/";
}

void UTILS::URL::RemovePipePart(std::string& url)
{
  const size_t urlPipePos = url.find("|");
  if (urlPipePos != std::string::npos)
    url.erase(urlPipePos);
}

bool UTILS::URL::IsValidUri(const std::string& uri, std::string_view scheme /* = "data" */)
{
  return uri.find("data:", 0) == 0;
}

bool UTILS::URL::GetUriByteData(std::string_view uri, std::vector<uint8_t>& data)
{
  // Uri data format: "data:[media type][;attribute=value][;base64],<data>"
  std::vector<std::string> colonSplit = STRING::SplitToVec(uri, ':');
  if (colonSplit.size() == 2 && colonSplit[0] == "data")
  {
    std::vector<std::string> semiColonSplit = STRING::SplitToVec(colonSplit[1], ';');
    if (semiColonSplit.size() > 0)
    {
      std::vector<std::string> comSplit = STRING::SplitToVec(semiColonSplit.back(), ',');
      if (comSplit.size() == 2)
      {
        const bool isBase64 = comSplit[0] == "base64";
        const std::string dataStr = comSplit[1];
        if (isBase64)
          data = BASE64::Decode(dataStr);
        else
          data = STRING::ToVecUint8(dataStr);
        return true;
      }
    }
    LOG::Log(LOGERROR, "Cannot parse URI: %s", uri.data());
    return true;
  }
  return false;
}
