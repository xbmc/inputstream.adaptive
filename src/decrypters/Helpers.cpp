/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Helpers.h"

#include "utils/Base64Utils.h"
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

std::vector<uint8_t> DRM::ConvertKidStrToBytes(std::string_view kidStr)
{
  if (kidStr.size() != 32)
  {
    LOG::LogF(LOGERROR, "Cannot convert KID \"%s\" as bytes due to wrong size", kidStr.data());
    return {};
  }

  std::vector<uint8_t> kidBytes(16, 0);
  const char* kidPtr = kidStr.data();

  for (size_t i{0}; i < 16; i++)
  {
    kidBytes[i] = STRING::ToHexNibble(*kidPtr) << 4;
    kidPtr++;
    kidBytes[i] |= STRING::ToHexNibble(*kidPtr);
    kidPtr++;
  }

  return kidBytes;
}

std::string DRM::ConvertKidBytesToUUID(std::vector<uint8_t> kid)
{
  if (kid.size() != 16)
    return {};

  static char hexDigits[] = "0123456789abcdef";
  std::string uuid;
  for (size_t i{0}; i < 16; ++i)
  {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      uuid += '-';
    uuid += hexDigits[kid[i] >> 4];
    uuid += hexDigits[kid[i] & 15];
  }
  return uuid;
}

std::vector<uint8_t> DRM::ConvertPrKidtoWvKid(std::vector<uint8_t> kid)
{
  if (kid.size() != 16)
    return {};

  std::vector<uint8_t> remapped;
  static const size_t remap[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
  // Reordering bytes
  for (size_t i{0}; i < 16; ++i)
  {
    remapped.emplace_back(kid[remap[i]]);
  }
  return remapped;
}

bool DRM::CreateISMlicense(std::string_view kidStr,
                           std::string_view licenseData,
                           std::vector<uint8_t>& initData)
{
  std::vector<uint8_t> kidBytes = ConvertKidStrToBytes(kidStr);

  if (kidBytes.size() != 16 || licenseData.empty())
  {
    initData.clear();
    return false;
  }

  std::string decLicData = BASE64::DecodeToStr(licenseData);
  size_t origLicenseSize = decLicData.size();

  const uint8_t* kid{reinterpret_cast<const uint8_t*>(std::strstr(decLicData.data(), "{KID}"))};
  const uint8_t* uuid{reinterpret_cast<const uint8_t*>(std::strstr(decLicData.data(), "{UUID}"))};
  uint8_t* decLicDataUint = reinterpret_cast<uint8_t*>(decLicData.data());

  size_t license_size = uuid ? origLicenseSize + 36 - 6 : origLicenseSize;

  //Build up proto header
  initData.resize(512);
  uint8_t* protoptr(initData.data());
  if (kid)
  {
    if (uuid && uuid < kid)
      return false;
    license_size -= 5; //Remove sizeof(placeholder)
    std::memcpy(protoptr, decLicDataUint, kid - decLicDataUint);
    protoptr += kid - decLicDataUint;
    license_size -= static_cast<size_t>(kid - decLicDataUint);
    kid += 5;
    origLicenseSize -= kid - decLicDataUint;
  }
  else
    kid = decLicDataUint;

  *protoptr++ = 18; //id=16>>3=2, type=2(flexlen)
  *protoptr++ = 16; //length of key
  std::memcpy(protoptr, kidBytes.data(), 16);
  protoptr += 16;

  *protoptr++ = 34; //id=32>>3=4, type=2(flexlen)
  do
  {
    *protoptr++ = static_cast<uint8_t>(license_size & 127);
    license_size >>= 7;
    if (license_size)
      *(protoptr - 1) |= 128;
    else
      break;
  } while (1);
  if (uuid)
  {
    std::memcpy(protoptr, kid, uuid - kid);
    protoptr += uuid - kid;

    std::string uuidKid{ConvertKidBytesToUUID(kidBytes)};
    protoptr = reinterpret_cast<uint8_t*>(uuidKid.data());

    size_t sizeleft = origLicenseSize - ((uuid - kid) + 6);
    std::memcpy(protoptr, uuid + 6, sizeleft);
    protoptr += sizeleft;
  }
  else
  {
    std::memcpy(protoptr, kid, origLicenseSize);
    protoptr += origLicenseSize;
  }
  initData.resize(protoptr - initData.data());

  return true;
}
