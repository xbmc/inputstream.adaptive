/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "HelperWv.h"
#include "Helpers.h"
#include "utils/Base64Utils.h"
#include "utils/DigestMD5Utils.h"
#include "utils/JsonUtils.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/XMLUtils.h"
#include "utils/log.h"

#include <pugixml.hpp>

#include <algorithm>
#include <map>

using namespace pugi;
using namespace UTILS;

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

// Supported wrappers
enum class Wrapper
{
  AUTO, // Try auto-detect wrappers
  NONE, // Implicit for raw binary data
  BASE64,
  JSON,
  XML,
  URL_ENC, // URL encode
};

// \brief Translate a wrapper string in to relative vector of enum values.
//        e.g. "json,base64" --> JSON, BASE64
std::vector<Wrapper> TranslateWrapper(std::string_view wrapper)
{
  const std::vector<std::string> wrappers = STRING::SplitToVec(wrapper, ',');

  if (wrappers.empty())
    return {};

  std::vector<Wrapper> result;
  // Here we have to keep the order because
  // defines the order in which data will be unwrapped
  for (const std::string& wrapper : wrappers)
  {
    if (wrapper == "auto")
      result.emplace_back(Wrapper::AUTO);
    else if (wrapper == "none")
      result.emplace_back(Wrapper::NONE);
    else if (wrapper == "base64")
      result.emplace_back(Wrapper::BASE64);
    else if (wrapper == "json")
      result.emplace_back(Wrapper::JSON);
    else if (wrapper == "xml")
      result.emplace_back(Wrapper::XML);
    else if (wrapper == "urlenc")
      result.emplace_back(Wrapper::URL_ENC);
    else
    {
      LOG::LogF(LOGERROR, "Cannot translate license wrapper, unknown type \"%s\"", wrapper.c_str());
      return {Wrapper::AUTO};
    }
  }
  return result;
}

//! @todo: to be removed in future when the old DRM properties will be removed
void ConvertDeprecatedPlaceholders(std::string& data)
{
  if (data.empty())
    return;

  if (STRING::Contains(data, "R{SSM}", false)) // Raw data
  {
    STRING::ReplaceFirst(data, "R{SSM}", "{CHA-RAW}");
  }
  else if (STRING::Contains(data, "b{SSM}", false)) // Base64 encoded
  {
    STRING::ReplaceFirst(data, "b{SSM}", "{CHA-B64}");
  }
  else if (STRING::Contains(data, "B{SSM}", false)) // Base64 and URL encoded
  {
    STRING::ReplaceFirst(data, "B{SSM}", "{CHA-B64U}");
  }
  else if (STRING::Contains(data, "D{SSM}", false)) // Decimal converted
  {
    STRING::ReplaceFirst(data, "D{SSM}", "{CHA-DEC}");
  }

  // SESSION ID - Placeholder {SID-?}

  if (STRING::Contains(data, "R{SID}", false)) // Raw
  {
    STRING::ReplaceFirst(data, "R{SID}", "{SID-RAW}");
  }
  else if (STRING::Contains(data, "b{SID}", false)) // Base64 encoded
  {
    STRING::ReplaceFirst(data, "b{SID}", "{SID-B64}");
  }
  else if (STRING::Contains(data, "B{SID}", false)) // Base64 and URL encoded
  {
    STRING::ReplaceFirst(data, "B{SID}", "{SID-B64U}");
  }

  // KEY ID - Placeholder {KID-?}

  if (STRING::Contains(data, "R{KID}", false)) // KID converted to UUID format
  {
    STRING::ReplaceFirst(data, "R{KID}", "{KID-UUID}");
  }
  else if (STRING::Contains(data, "H{KID}", false)) // Hexadecimal converted
  {
    STRING::ReplaceFirst(data, "H{KID}", "{KID-HEX}");
  }

  // PSSH - Placeholder {PSSH-?}

  if (STRING::Contains(data, "b{PSSH}", false)) // Base64 encoded
  {
    STRING::ReplaceFirst(data, "b{PSSH}", "{PSSH-B64}");
  }
  else if (STRING::Contains(data, "B{PSSH}", false)) // Base64 and URL encoded
  {
    STRING::ReplaceFirst(data, "B{PSSH}", "{PSSH-B64U}");
  }
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

bool DRM::WvWrapLicense(std::string& data,
                        const std::vector<uint8_t>& challenge,
                        std::string_view sessionId,
                        const std::vector<uint8_t>& kid,
                        const std::vector<uint8_t>& pssh,
                        std::string_view wrapper,
                        const bool isNewConfig)
{
  //! @todo: to be removed in future when the old DRM properties will be removed
  if (!isNewConfig)
    ConvertDeprecatedPlaceholders(data);

  // By default raw key request (challenge) data
  if (data.empty())
    data = "{CHA-RAW}";

  // KEY REQUEST (CHALLENGE) - Placeholder {CHA-?}

  if (STRING::Contains(data, "{CHA-RAW}", false)) // Raw data
  {
    std::string_view krStr{reinterpret_cast<const char*>(challenge.data()), challenge.size()};
    STRING::ReplaceFirst(data, "{CHA-RAW}", krStr);
  }
  else if (STRING::Contains(data, "{CHA-B64}", false)) // Base64 encoded
  {
    STRING::ReplaceFirst(data, "{CHA-B64}", BASE64::Encode(challenge));
  }
  else if (STRING::Contains(data, "{CHA-B64U}", false)) // Base64 and URL encoded
  {
    const std::string krEnc = STRING::URLEncode(BASE64::Encode(challenge));
    STRING::ReplaceFirst(data, "{CHA-B64U}", krEnc);
  }
  else if (STRING::Contains(data, "{CHA-DEC}", false)) // Decimal converted
  {
    const std::string krDec = STRING::ToDecimal(challenge.data(), challenge.size());
    STRING::ReplaceFirst(data, "{CHA-DEC}", krDec);
  }

  // SESSION ID - Placeholder {SID-?}

  if (STRING::Contains(data, "{SID-RAW}", false)) // Raw
  {
    STRING::ReplaceFirst(data, "{SID-RAW}", sessionId);
  }
  else if (STRING::Contains(data, "{SID-B64}", false)) // Base64 encoded
  {
    STRING::ReplaceFirst(data, "{SID-B64}", BASE64::Encode(sessionId.data()));
  }
  else if (STRING::Contains(data, "{SID-B64U}", false)) // Base64 and URL encoded
  {
    const std::string sidEnc = STRING::URLEncode(BASE64::Encode(sessionId.data()));
    STRING::ReplaceFirst(data, "{SID-B64U}", sidEnc);
  }

  // KEY ID - Placeholder {KID-?}

  if (STRING::Contains(data, "{KID-UUID}", false)) // KID converted to UUID format
  {
    STRING::ReplaceFirst(data, "{KID-UUID}", DRM::ConvertKidBytesToUUID(kid));
  }
  else if (STRING::Contains(data, "{KID-HEX}", false)) // Hexadecimal converted
  {
    STRING::ReplaceFirst(data, "{KID-HEX}", STRING::ToHexadecimal(kid));
  }

  // PSSH - Placeholder {PSSH-?}

  if (STRING::Contains(data, "{PSSH-B64}", false)) // Base64 encoded
  {
    STRING::ReplaceFirst(data, "{PSSH-B64}", BASE64::Encode(pssh));
  }
  else if (STRING::Contains(data, "{PSSH-B64U}", false)) // Base64 and URL encoded
  {
    std::string sidEnc = STRING::URLEncode(BASE64::Encode(pssh));
    STRING::ReplaceFirst(data, "{PSSH-B64U}", sidEnc);
  }

  const std::vector<Wrapper> wrappers = TranslateWrapper(wrapper);

  for (size_t i = 0; i < wrappers.size(); ++i)
  {
    const auto& wrapper = wrappers[i];

    if (wrapper == Wrapper::NONE)
    {
      break;
    }
    else if (wrapper == Wrapper::BASE64)
    {
      data = BASE64::Encode(data);
    }
    else if (wrapper == Wrapper::URL_ENC)
    {
      data = STRING::URLEncode(data);
    }
    else
    {
      LOG::LogF(LOGERROR, "Specified an unsupported wrapper type");
      return false;
    }
  }

  return true;
}

bool DRM::WvUnwrapLicense(std::string_view wrapper,
                          const std::map<std::string, std::string>& params,
                          std::string_view contentType,
                          std::string data,
                          std::string& dataOut,
                          int& hdcpLimit)
{
  // The license response must be in binary data format
  // but many services have a proprietary implementations therefore
  // the license data could be wrapped by using other formats (such as base64, json, etc...)
  // here we provide the support for some common wrappers,
  // for more complex requirements the audio/video add-on must implement a proxy
  // where it can request and process the license in a custom way and return the binary data

  std::vector<Wrapper> wrappers = TranslateWrapper(wrapper);

  const bool isAuto = wrappers.empty() || wrappers.front() == Wrapper::AUTO;

  bool isAllowedFallbacks{false};

  if (isAuto)
  {
    wrappers.clear();
    // Check mime types to try detect the wrapper
    if (contentType == "application/octet-stream")
    {
      // its binary
    }
    else if (contentType == "application/json")
    {
      if (BASE64::IsValidBase64(data))
        wrappers.emplace_back(Wrapper::BASE64);

      wrappers.emplace_back(Wrapper::JSON);
    }
    else if (contentType == "application/xml" || contentType == "text/xml")
    {
      wrappers.emplace_back(Wrapper::XML);
    }
    else if (contentType == "text/plain")
    {
      // Some service use text mime type for XML
      isAllowedFallbacks = true;
      wrappers.emplace_back(Wrapper::XML);
    }
    else // Unknown
    {
      // Assumed to be binary with a possible base64 wrap
      if (BASE64::IsValidBase64(data))
        wrappers.emplace_back(Wrapper::BASE64);
    }
  }

  // Process multiple wrappers with sequential order

  for (size_t i = 0; i < wrappers.size(); ++i)
  {
    const auto& wrapper = wrappers[i];

    if (wrapper == Wrapper::NONE)
    {
      break;
    }
    else if (wrapper == Wrapper::BASE64)
    {
      data = BASE64::DecodeToStr(data);
    }
    else if (wrapper == Wrapper::JSON)
    {
      if (!STRING::KeyExists(params, "path_data"))
      {
        LOG::LogF(LOGERROR,
                  "Cannot parse JSON license data, missing unwrapper parameter \"path_data\"");
        return false;
      }

      rapidjson::Document jDoc;
      jDoc.Parse(data.c_str(), data.size());

      if (!jDoc.IsObject())
      {
        LOG::LogF(LOGERROR,
                  "Unable to parse license data as JSON format, malformed data or wrong wrapper");
        return false;
      }

      bool isJsonDataTraverse{false};
      if (STRING::KeyExists(params, "path_data_traverse"))
        isJsonDataTraverse = STRING::ToLower(params.at("path_data_traverse")) == "true";

      const rapidjson::Value* jDataObjValue;
      if (isJsonDataTraverse)
        jDataObjValue = JSON::GetValueTraversePaths(jDoc, params.at("path_data"));
      else
        jDataObjValue = JSON::GetValueAtPath(jDoc, params.at("path_data"));

      if (!jDataObjValue || !jDataObjValue->IsString())
      {
        LOG::LogF(LOGERROR, "Unable to get license data from JSON path, possible wrong path on "
                            "\"path_data\" parameter");
        return false;
      }

      data = jDataObjValue->GetString();

      if (STRING::KeyExists(params, "path_hdcp"))
      {
        bool isJsonHdcpTraverse{false};
        if (STRING::KeyExists(params, "path_hdcp_traverse"))
          isJsonHdcpTraverse = STRING::ToLower(params.at("path_hdcp_traverse")) == "true";

        const rapidjson::Value* jHdcpObjValue;
        if (isJsonHdcpTraverse)
          jHdcpObjValue = JSON::GetValueTraversePaths(jDoc, params.at("path_hdcp"));
        else
          jHdcpObjValue = JSON::GetValueAtPath(jDoc, params.at("path_hdcp"));

        if (!jHdcpObjValue)
        {
          LOG::LogF(LOGERROR, "Unable to parse JSON HDCP value, path \"%s\" not found",
                    params.at("path_hdcp").c_str());
        }
        else if (!jHdcpObjValue->IsInt())
        {
          LOG::LogF(LOGERROR,
                    "Unable to parse JSON HDCP value, value with wrong data type on path \"%s\"",
                    params.at("path_hdcp").c_str());
        }
        else
          hdcpLimit = jHdcpObjValue->GetInt();
      }

      if (isAuto && BASE64::IsValidBase64(data))
        wrappers.emplace_back(Wrapper::BASE64);
    }
    else if (wrapper == Wrapper::XML)
    {
      if (!STRING::KeyExists(params, "path_data"))
      {
        LOG::LogF(LOGERROR,
                  "Cannot parse XML license data, missing unwrapper parameter \"path_data\"");
        return false;
      }

      xml_document doc;
      xml_parse_result parseRes = doc.load_buffer(data.c_str(), data.size());

      if (parseRes.status != status_ok)
      {
        if (isAllowedFallbacks)
        {
          LOG::LogF(LOGDEBUG, "License data not in XML format, fallback to binary");
          wrappers.emplace_back(Wrapper::NONE);
          continue;
        }
        else
        {
          LOG::LogF(LOGERROR, "Unable to parse XML license data, malformed data or wrong wrapper");
          return false;
        }
      }

      bool isTagTraverse{false};
      if (STRING::KeyExists(params, "path_data_traverse"))
        isTagTraverse = STRING::ToLower(params.at("path_data_traverse")) == "true";

      pugi::xml_node node;
      if (isTagTraverse)
        node = XML::GetNodeTraverseTags(doc.first_child(), params.at("path_data"));
      else
        node = doc.select_node(params.at("path_data").c_str()).node();

      if (!node)
      {
        LOG::LogF(LOGERROR, "Unable to get license data from XML path \"%s\"",
                  params.at("path_data").c_str());
        return false;
      }
      data = node.child_value();

      if (isAuto && BASE64::IsValidBase64(data))
        wrappers.emplace_back(Wrapper::BASE64);
    }
    else
    {
      LOG::LogF(LOGERROR, "Specified an unsupported unwrapper type");
      return false;
    }
  }

  if (data.empty())
  {
    LOG::LogF(LOGERROR, "No license data, a problem occurred while processing license wrappers");
    return false;
  }

  //! @todo: the support to binary license data (with HB) that start with "\r\n\r\n" has not been reintroduced with the
  //! rework of this code, this is a old unclear addition, seem there are no info about this on web,
  //! and seem no addons use it, so for now is removed, if in the future someone complain about this lack
  //! will be possible reintroduce it and include more clear info about this use case.
  // if (data.compare(0, 4, "\r\n\r\n") == 0)
  //   data.erase(0, 4);

  dataOut = data;

  return true;
}

void DRM::TranslateLicenseUrlPh(std::string& url,
                                const std::vector<uint8_t>& challenge,
                                const bool isNewConfig)
{
  if (!isNewConfig)
  {
    // Replace deprecated placeholders
    //! @todo: to be removed in future when the old DRM properties will be removed
    if (STRING::Contains(url, "B{SSM}", false)) // Base64 and URL encoded
      STRING::ReplaceFirst(url, "B{SSM}", "{CHA-B64U}");
    if (STRING::Contains(url, "{HASH}", false)) // MD5 hash
      STRING::ReplaceFirst(url, "{HASH}", "{CHA-MD5}");
  }

  if (STRING::Contains(url, "{CHA-B64U}", false))  // Base64 and URL encoded
  {
    const std::string krEnc = STRING::URLEncode(BASE64::Encode(challenge));
    STRING::ReplaceFirst(url, "{CHA-B64U}", krEnc);
  }
  else if (STRING::Contains(url, "{CHA-MD5}", false)) // MD5 hash
  {
    DIGEST::MD5 md5;
    md5.Update(challenge.data(), static_cast<uint32_t>(challenge.size()));
    md5.Finalize();
    STRING::ReplaceFirst(url, "{CHA-MD5}", md5.HexDigest());
  }
}
