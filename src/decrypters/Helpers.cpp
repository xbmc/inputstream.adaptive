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

#include <algorithm>

using namespace UTILS;

namespace
{
constexpr uint8_t PSSHBOX_HEADER_PSSH[4] = {0x70, 0x73, 0x73, 0x68};
constexpr uint8_t PSSHBOX_HEADER_VER0[4] = {0x00, 0x00, 0x00, 0x00};

/*!
 * \brief Make a protobuf tag.
 * \param fieldNumber The field number
 * \param wireType The wire type:
 *                 0 = varint (int32, int64, uint32, uint64, sint32, sint64, bool, enum)
 *                 1 = 64 bit (fixed64, sfixed64, double)
 *                 2 = Length-delimited (string, bytes, embedded messages, packed repeated fields)
 *                 5 = 32 bit (fixed32, sfixed32, float)
 * \return The tag.
 */
int MakeProtobufTag(int fieldNumber, int wireType)
{
  return (fieldNumber << 3) | wireType;
}

// \brief Write the size value to the data as varint format
void WriteProtobufVarintSize(std::vector<uint8_t>& data, int size)
{
  do
  {
    uint8_t byte = size & 127;
    size >>= 7;
    if (size > 0)
      byte |= 128; // Varint continuation
    data.emplace_back(byte);
  } while (size > 0);
}

/*!
 * \brief Replace in a vector, a sequence of vector data with another one.
 * \param data The data to be modified
 * \param sequence The sequence of data to be searched
 * \param replace The data used to replace the sequence
 * \return True if the data has been modified, otherwise false.
 */
bool ReplaceVectorSeq(std::vector<uint8_t>& data,
                      const std::vector<uint8_t>& sequence,
                      const std::vector<uint8_t>& replace)
{
  auto it = std::search(data.begin(), data.end(), sequence.begin(), sequence.end());
  if (it != data.end())
  {
    it = data.erase(it, it + sequence.size());
    data.insert(it, replace.begin(), replace.end());
    return true;
  }
  return false;
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

std::vector<uint8_t> DRM::ConvertKidToUUIDVec(const std::vector<uint8_t>& kid)
{
  if (kid.size() != 16)
    return {};

  static char hexDigits[] = "0123456789abcdef";
  std::vector<uint8_t> uuid;
  uuid.reserve(32);

  for (size_t i = 0; i < 16; ++i)
  {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      uuid.emplace_back('-');

    uuid.emplace_back(hexDigits[kid[i] >> 4]);
    uuid.emplace_back(hexDigits[kid[i] & 15]);
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

bool DRM::IsValidPsshHeader(const std::vector<uint8_t>& pssh)
{
  return pssh.size() >= 8 && std::equal(pssh.begin() + 4, pssh.begin() + 8, PSSHBOX_HEADER_PSSH);
}

bool DRM::MakeWidevinePsshData(const std::vector<uint8_t>& kid,
                               std::vector<uint8_t> contentIdData,
                               std::vector<uint8_t>& wvPsshData)
{
  wvPsshData.clear();

  if (kid.empty())
    return false;

  // The generated synthesized Widevine PSSH box require minimal contents:
  // - The key_id field set with the KID
  // - The content_id field copied from the key_id field (but we allow custom content)

  // Create "key_id" field, id: 2 (can be repeated if multiples)
  wvPsshData.push_back(MakeProtobufTag(2, 2));
  WriteProtobufVarintSize(wvPsshData, static_cast<int>(kid.size()));
  wvPsshData.insert(wvPsshData.end(), kid.begin(), kid.end());

  // Prepare "content_id" data
  if (contentIdData.empty()) // If no data, by default add the KID
  {
    contentIdData.insert(contentIdData.end(), kid.begin(), kid.end());
  }
  else
  {
    // Replace placeholders if needed
    static const std::vector<uint8_t> phKid = {'{', 'K', 'I', 'D', '}'};
    ReplaceVectorSeq(contentIdData, phKid, kid);

    static const std::vector<uint8_t> phUuid = {'{', 'U', 'U', 'I', 'D', '}'};
    const std::vector<uint8_t> kidUuid = ConvertKidToUUIDVec(kid);
    ReplaceVectorSeq(contentIdData, phUuid, kidUuid);
  }

  // Create "content_id" field, id: 4
  wvPsshData.push_back(MakeProtobufTag(4, 2));
  WriteProtobufVarintSize(wvPsshData, static_cast<int>(contentIdData.size()));
  wvPsshData.insert(wvPsshData.end(), contentIdData.begin(), contentIdData.end());

  return true;
}

bool DRM::MakePssh(const uint8_t* systemId,
                   const std::vector<uint8_t>& initData,
                   std::vector<uint8_t>& psshData)
{
  if (!systemId)
    return false;

  psshData.clear();
  psshData.resize(4, 0); // Size field 4 bytes (updated later)
  psshData.insert(psshData.end(), PSSHBOX_HEADER_PSSH, PSSHBOX_HEADER_PSSH + 4);
  psshData.insert(psshData.end(), PSSHBOX_HEADER_VER0, PSSHBOX_HEADER_VER0 + 4);
  psshData.insert(psshData.end(), systemId, systemId + 16);

  // Add init data size (4 bytes)
  const uint32_t initDataSize = static_cast<uint32_t>(initData.size());
  psshData.emplace_back(static_cast<uint8_t>((initDataSize >> 24) & 0xFF));
  psshData.emplace_back(static_cast<uint8_t>((initDataSize >> 16) & 0xFF));
  psshData.emplace_back(static_cast<uint8_t>((initDataSize >> 8) & 0xFF));
  psshData.emplace_back(static_cast<uint8_t>(initDataSize & 0xFF));

  // Add init data
  psshData.insert(psshData.end(), initData.begin(), initData.end());

  // Update box size (first 4 bytes)
  const uint32_t boxSize = static_cast<uint32_t>(psshData.size());
  psshData[0] = static_cast<uint8_t>((boxSize >> 24) & 0xFF);
  psshData[1] = static_cast<uint8_t>((boxSize >> 16) & 0xFF);
  psshData[2] = static_cast<uint8_t>((boxSize >> 8) & 0xFF);
  psshData[3] = static_cast<uint8_t>(boxSize & 0xFF);

  return true;
}
