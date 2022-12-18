/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "UrlUtils.h"

#include "StringUtils.h"

using namespace UTILS::URL;

namespace
{
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

  if (validateLenght && url.size() >= 2083)
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
} // unnamed namespace

bool UTILS::URL::IsValidUrl(const std::string& url)
{
  return isUrl(url, false, true, true, true, true, false);
}

bool UTILS::URL::IsUrlAbsolute(std::string_view url)
{
  return (url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0);
}

bool UTILS::URL::IsUrlRelative(std::string_view url)
{
  return (url.compare(0, 1, "/") == 0);
}

bool UTILS::URL::IsUrlRelativeLevel(std::string_view url)
{
  return (url.compare(0, 3, "../") == 0);
}

std::string UTILS::URL::GetParametersFromPlaceholder(std::string& url, std::string_view placeholder)
{
  std::string::size_type phPos = url.find(placeholder);
  if (phPos != std::string::npos)
  {
    while (phPos && url[phPos] != '&' && url[phPos] != '?')
    {
      --phPos;
    }
    if (phPos > 0)
      return url.substr(phPos);
  }
  return "";
}

std::string UTILS::URL::GetParameters(std::string& url) {
  size_t paramsPos = url.find('?');
  if (paramsPos != std::string::npos)
    return url.substr(paramsPos + 1);

  return "";
}

std::string UTILS::URL::RemoveParameters(std::string url, bool removeFilenameParam /* = true */)
{
  size_t paramsPos = url.find('?');
  if (paramsPos != std::string::npos)
    url.resize(paramsPos);

  if (removeFilenameParam)
  {
    size_t slashPos = url.find_last_of('/');
    if (slashPos != std::string::npos && slashPos != (url.find("://") + 2))
      url.resize(slashPos + 1);
  }
  return url;
}

void UTILS::URL::AppendParameters(std::string& url, std::string params)
{
  if (params.empty())
    return;

  if (url.find_first_of('?') == std::string::npos)
    url += "?";
  else
    url += "&";

  if (params.front() == '&' || params.front() == '?')
    params.pop_back();

  url += params;
}

std::string UTILS::URL::GetDomainUrl(std::string url)
{
  if (IsUrlAbsolute(url))
  {
    size_t paramsPos = url.find('?');
    if (paramsPos != std::string::npos)
      url = url.substr(0, paramsPos);

    size_t slashPos = url.find_first_of('/', url.find("://") + 3);
    if (slashPos != std::string::npos)
      url = url.substr(0, slashPos);
  }
  if (url.back() == '/')
    url.pop_back();

  return url;
}

std::string UTILS::URL::Join(std::string baseUrl, std::string otherUrl)
{
  if (baseUrl.empty())
    return otherUrl;

  if (baseUrl.back() == '/')
    baseUrl.pop_back();

  if (IsUrlRelativeLevel(otherUrl))
  {
    // Join the otherUrl to the relative level path of the baseUrl,
    // if the baseUrl do not have directory levels available will be appended
    static const std::string_view relativeChars{"../"};
    size_t pos{0};
    std::string parentBaseUrl{baseUrl};

    size_t addrsStartPos{1};
    if (IsUrlAbsolute(baseUrl))
      addrsStartPos = baseUrl.find("://") + 3;
    else if (IsUrlRelativeLevel(baseUrl))
      addrsStartPos = 3;

    // Loop to go back in to each base URL directory level
    while ((pos = otherUrl.find(relativeChars, pos)) != std::string::npos)
    {
      std::size_t lastSlashPos = parentBaseUrl.find_last_of('/');
      if ((lastSlashPos + 1) == addrsStartPos)
        break;
      parentBaseUrl = parentBaseUrl.substr(0, lastSlashPos);
      pos += relativeChars.size();
    }
    STRING::ReplaceAll(otherUrl, relativeChars, "");
    return parentBaseUrl + "/" + otherUrl;
  }
  if (IsUrlRelative(otherUrl))
  {
    // Join the otherUrl to the domain of the baseUrl
    return GetDomainUrl(baseUrl) + otherUrl;
  }
  return baseUrl + "/" + otherUrl;
}
