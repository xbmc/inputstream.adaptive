/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "HelperWv.h"
#include "utils/log.h"

#include <algorithm>

namespace
{
constexpr uint8_t PSSHBOX_HEADER_PSSH[4] = {0x70, 0x73, 0x73, 0x68};
constexpr uint8_t PSSHBOX_HEADER_VER0[4] = {0x00, 0x00, 0x00, 0x00};

// Protection scheme identifying the encryption algorithm. The protection
// scheme is represented as a uint32 value. The uint32 contains 4 bytes each
// representing a single ascii character in one of the 4CC protection scheme values.
enum class WIDEVINE_PROT_SCHEME
{
  CENC = 0x63656E63,
  CBC1 = 0x63626331,
  CENS = 0x63656E73,
  CBCS = 0x63626373
};

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

// \brief Write a protobuf varint value to the data
void WriteProtobufVarint(std::vector<uint8_t>& data, int size)
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

// \brief Read a protobuf varint value from the data
int ReadProtobufVarint(const std::vector<uint8_t>& data, size_t& offset)
{
  int value = 0;
  int shift = 0;
  while (true)
  {
    uint8_t byte = data[offset++];
    value |= (byte & 0x7F) << shift;
    if (!(byte & 0x80))
      break;
    shift += 7;
  }
  return value;
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

std::vector<uint8_t> ConvertKidToUUIDVec(const std::vector<uint8_t>& kid)
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
} // unnamed namespace

std::vector<uint8_t> DRM::MakeWidevinePsshData(const std::vector<std::vector<uint8_t>>& keyIds,
                                               std::vector<uint8_t> contentIdData)
{
  std::vector<uint8_t> wvPsshData;

  if (keyIds.empty())
  {
    LOG::LogF(LOGERROR, "Cannot make Widevine PSSH, key id's must be supplied");
    return {};
  }

  // The generated synthesized Widevine PSSH box require minimal contents:
  // - The key_id field set with the KID
  // - The content_id field copied from the key_id field (but we allow custom content)

  // Create "key_id" field, id: 2 (repeated if multiples)
  for (const std::vector<uint8_t>& keyId : keyIds)
  {
    wvPsshData.push_back(MakeProtobufTag(2, 2));
    WriteProtobufVarint(wvPsshData, static_cast<int>(keyId.size())); // Write data size
    wvPsshData.insert(wvPsshData.end(), keyId.begin(), keyId.end());
  }

  // Prepare "content_id" data
  const std::vector<uint8_t>& keyId = keyIds.front();

  if (contentIdData.empty()) // If no data, by default add the KID if single
  {
    if (keyIds.size() == 1)
      contentIdData.insert(contentIdData.end(), keyId.begin(), keyId.end());
  }
  else
  {
    // Replace placeholders if needed
    static const std::vector<uint8_t> phKid = {'{', 'K', 'I', 'D', '}'};
    ReplaceVectorSeq(contentIdData, phKid, keyId);

    static const std::vector<uint8_t> phUuid = {'{', 'U', 'U', 'I', 'D', '}'};
    const std::vector<uint8_t> kidUuid = ConvertKidToUUIDVec(keyId);
    ReplaceVectorSeq(contentIdData, phUuid, kidUuid);
  }

  if (!contentIdData.empty())
  {
    // Create "content_id" field, id: 4  
    wvPsshData.push_back(MakeProtobufTag(4, 2));
    WriteProtobufVarint(wvPsshData, static_cast<int>(contentIdData.size())); // Write data size
    wvPsshData.insert(wvPsshData.end(), contentIdData.begin(), contentIdData.end());
  }

  // Create "protection_scheme" field, id: 9
  // wvPsshData.push_back(MakeProtobufTag(9, 0));
  // WriteProtobufVarint(wvPsshData, static_cast<int>(WIDEVINE_PROT_SCHEME::CENC));

  return wvPsshData;
}

void DRM::ParseWidevinePssh(const std::vector<uint8_t>& wvPsshData,
                            std::vector<std::vector<uint8_t>>& keyIds)
{
  keyIds.clear();

  size_t offset = 0;
  while (offset < wvPsshData.size())
  {
    uint8_t tag = wvPsshData[offset++];
    int fieldNumber = tag >> 3;
    int wireType = tag & 0x07;

    if (fieldNumber == 2 && wireType == 2) // "key_id" field, id: 2
    {
      int length = ReadProtobufVarint(wvPsshData, offset);
      keyIds.emplace_back(wvPsshData.begin() + offset, wvPsshData.begin() + offset + length);
    }
    else // Skip other fields
    {
      int length = ReadProtobufVarint(wvPsshData, offset);
      if (wireType != 0) // Wire type 0 has no field size
        offset += length;
    }
  }
}
