/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Helpers.h"

#include "HelperPr.h"
#include "HelperWv.h"
#include "utils/Base64Utils.h"
#include "utils/CharArrayParser.h"
#include "utils/DigestMD5Utils.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/log.h"

#include <algorithm>

using namespace UTILS;

namespace
{
constexpr uint8_t PSSHBOX_HEADER_PSSH[4] = {0x70, 0x73, 0x73, 0x68};

void WriteBigEndianInt(std::vector<uint8_t>& data, const uint32_t value)
{
  data.emplace_back(static_cast<uint8_t>((value >> 24) & 0xFF));
  data.emplace_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  data.emplace_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  data.emplace_back(static_cast<uint8_t>(value & 0xFF));
}

} // unnamed namespace

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

std::vector<std::string> DRM::UrnsToSystemIds(const std::vector<std::string_view>& urns)
{
  std::vector<std::string> sids;

  for (std::string_view urn : urns)
  {
    std::string sid = DRM::UrnToSystemId(urn);
    if (!sid.empty())
      sids.emplace_back(DRM::UrnToSystemId(urn));
  }

  return sids;
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

bool DRM::IsValidPsshHeader(const std::vector<uint8_t>& pssh)
{
  return pssh.size() >= 8 && std::equal(pssh.begin() + 4, pssh.begin() + 8, PSSHBOX_HEADER_PSSH);
}

std::vector<uint8_t> DRM::PSSH::Make(const uint8_t* systemId,
                                     const std::vector<std::vector<uint8_t>>& keyIds,
                                     const std::vector<uint8_t>& initData,
                                     const uint8_t version,
                                     const uint32_t flags)
{
  if (!systemId)
  {
    LOG::LogF(LOGERROR, "Cannot make PSSH, no system id");
    return {};
  }
  if (version > 1)
  {
    LOG::LogF(LOGERROR, "Cannot make PSSH, version %u not supported", version);
    return {};
  }
  if (initData.empty() && keyIds.empty())
  {
    LOG::LogF(LOGERROR, "Cannot make PSSH, init data or key id's must be supplied");
    return {};
  }

  std::vector<uint8_t> psshBox;
  psshBox.resize(4, 0); // Size field of 4 bytes (updated later)

  psshBox.insert(psshBox.end(), PSSHBOX_HEADER_PSSH, PSSHBOX_HEADER_PSSH + 4);

  psshBox.emplace_back(version);

  psshBox.push_back((flags >> 16) & 0xFF);
  psshBox.push_back((flags >> 8) & 0xFF);
  psshBox.push_back(flags & 0xFF);

  psshBox.insert(psshBox.end(), systemId, systemId + 16);

  if (version == 1) // If version 1, add KID's
  {
    WriteBigEndianInt(psshBox, static_cast<uint32_t>(keyIds.size()));
    for (const std::vector<uint8_t>& keyId : keyIds)
    {
      if (keyId.size() != 16)
      {
        LOG::LogF(LOGERROR, "Cannot make PSSH, wrong KID size");
        return {};
      }
      psshBox.insert(psshBox.end(), keyId.begin(), keyId.end());
    }
  }

  // Add init data size
  WriteBigEndianInt(psshBox, static_cast<uint32_t>(initData.size()));

  // Add init data
  psshBox.insert(psshBox.end(), initData.begin(), initData.end());

  // Update box size (first 4 bytes)
  const uint32_t boxSize = static_cast<uint32_t>(psshBox.size());
  psshBox[0] = static_cast<uint8_t>((boxSize >> 24) & 0xFF);
  psshBox[1] = static_cast<uint8_t>((boxSize >> 16) & 0xFF);
  psshBox[2] = static_cast<uint8_t>((boxSize >> 8) & 0xFF);
  psshBox[3] = static_cast<uint8_t>(boxSize & 0xFF);

  return psshBox;
}

std::vector<uint8_t> DRM::PSSH::MakeWidevine(const std::vector<std::vector<uint8_t>>& keyIds,
                                             const std::vector<uint8_t>& initData,
                                             const uint8_t version,
                                             const uint32_t flags)
{
  // Make Widevine pssh data
  const std::vector<uint8_t> wvPsshData = DRM::MakeWidevinePsshData(keyIds, initData);
  if (wvPsshData.empty())
    return {};

  return Make(ID_WIDEVINE, keyIds, wvPsshData);
}

bool DRM::PSSH::Parse(const std::vector<uint8_t>& data)
{
  ResetData();
  CCharArrayParser charParser;
  charParser.Reset(data.data(), data.size());

  // BMFF box header (4byte size + 4byte type)
  if (charParser.CharsLeft() < 8)
  {
    LOG::LogF(LOGERROR, "Cannot parse PSSH data, malformed data.");
    return false;
  }
  const uint32_t boxSize = charParser.ReadNextUnsignedInt();
  
  if (!std::equal(charParser.GetDataPos(), charParser.GetDataPos() + 4, PSSHBOX_HEADER_PSSH))
  {
    LOG::LogF(LOGERROR, "Cannot parse PSSH data, no PSSH box type.");
    return false;
  }
  charParser.SkipChars(4);

  // Box header
  if (charParser.CharsLeft() < 4)
  {
    LOG::LogF(LOGERROR, "Cannot parse PSSH data, malformed data.");
    return false;
  }

  const uint32_t header = charParser.ReadNextUnsignedInt();

  m_version = (header >> 24) & 0x000000FF;
  m_flags = header & 0x00FFFFFF;

  // SystemID
  if (charParser.CharsLeft() < 16)
  {
    LOG::LogF(LOGERROR, "Cannot parse PSSH data, malformed data.");
    return false;
  }
  charParser.ReadNextArray(16, m_systemId);

  if (m_version == 1) // If version 1, get key id's from pssh field
  {
    // KeyIDs
    if (charParser.CharsLeft() < 4)
    {
      LOG::LogF(LOGERROR, "Cannot parse PSSH data, malformed data.");
      return false;
    }

    uint32_t kidCount = charParser.ReadNextUnsignedInt();
    while (kidCount > 0)
    {
      if (charParser.CharsLeft() < 16)
      {
        LOG::LogF(LOGERROR, "Cannot parse PSSH data, malformed data.");
        return false;
      }

      std::vector<uint8_t> kid;
      if (charParser.ReadNextArray(16, kid))
        m_keyIds.emplace_back(kid);

      kidCount--;
    }
  }

  // Get init data
  if (charParser.CharsLeft() < 4)
  {
    LOG::LogF(LOGERROR, "Cannot parse PSSH data, malformed data.");
    return false;
  }
  const uint32_t dataSize = charParser.ReadNextUnsignedInt();
  if (!charParser.ReadNextArray(dataSize, m_initData))
  {
    LOG::LogF(LOGERROR, "Cannot parse PSSH data, malformed data.");
    return false;
  }

  // Parse init data, where needed

  if (std::equal(m_systemId.cbegin(), m_systemId.cend(), ID_WIDEVINE))
  {
    if (m_version == 0)
      DRM::ParseWidevinePssh(m_initData, m_keyIds);
  }
  else if (m_version == 0 && std::equal(m_systemId.cbegin(), m_systemId.cend(), ID_PLAYREADY))
  {
    DRM::PRHeaderParser hParser;
    if (hParser.Parse(m_initData))
    {
      if (m_version == 0)
        m_keyIds.emplace_back(hParser.GetKID());

      m_licenseUrl = hParser.GetLicenseURL();
    }
  }

  return true;
}

void DRM::PSSH::ResetData()
{
  m_version = 0;
  m_flags = 0;
  m_systemId.clear();
  m_keyIds.clear();
  m_initData.clear();
  m_licenseUrl.clear();
}
