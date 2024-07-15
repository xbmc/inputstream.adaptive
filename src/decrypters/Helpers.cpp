/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Helpers.h"

#include "utils/DigestMD5Utils.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/log.h"

using namespace UTILS;

std::string DRM::GenerateUrlDomainHash(std::string_view url)
{
  std::string baseDomain = URL::GetBaseDomain(url.data());
  // If we are behind a proxy we fall always in to the same domain e.g. "http://localhost/"
  // but we have to differentiate the results based on the service of the add-on hosting the proxy
  // to avoid possible collisions, so we include the first directory path after the domain name
  // e.g. http://localhost:1234/addonservicename/other_dir/get_license?id=xyz
  // domain will result: http://localhost/addonservicename/
  if (STRING::Contains(baseDomain, "127.0.0.1") || STRING::Contains(baseDomain, "localhost"))
  {
    const size_t domainStartPos = url.find("://") + 3;
    const size_t pathStartPos = url.find_first_of('/', domainStartPos);
    if (pathStartPos != std::string::npos)
    {
      // Try get the first directory path name
      const size_t nextSlashPos = url.find_first_of('/', pathStartPos + 1);
      size_t length = std::string::npos;
      if (nextSlashPos != std::string::npos)
      {
        length = nextSlashPos - pathStartPos;
        baseDomain += url.substr(pathStartPos, length);
      }
    }
  }

  // Generate hash of domain name
  DIGEST::MD5 md5;
  md5.Update(baseDomain.c_str(), static_cast<uint32_t>(baseDomain.size()));
  md5.Finalize();
  return md5.HexDigest();
}

std::string DRM::UrnToSystemId(std::string_view urn)
{
  std::string sysId{urn.substr(9)}; // Remove prefix "urn:uuid:"
  STRING::ReplaceAll(sysId, "-", "");

  if (sysId.size() != 32)
  {
    LOG::Log(LOGERROR, "Cannot convert URN (%s) to System ID", urn.data());
    return "";
  }
  return sysId;
}

bool DRM::IsKeySystemSupported(std::string_view keySystem)
{
  return keySystem == DRM::KS_NONE || keySystem == DRM::KS_WIDEVINE ||
    keySystem == DRM::KS_PLAYREADY || keySystem == DRM::KS_WISEPLAY ||
    keySystem == DRM::KS_CLEARKEY;
}
