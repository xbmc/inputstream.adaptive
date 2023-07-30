/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Utils.h"

#include "Base64Utils.h"
#include "StringUtils.h"
#include "kodi/tools/StringUtils.h"

#include <algorithm> // any_of
#include <chrono>
#include <cstring>
#include <ctime>
#include <stdio.h>

using namespace UTILS;
using namespace kodi::tools;

std::string UTILS::AnnexbToHvcc(const char* b16Data)
{
  size_t sz = strlen(b16Data) >> 1;
  size_t szRun(sz);
  std::string result;

  if (sz > 1024)
    return result;

  uint8_t buffer[1024], *data(buffer);
  while (szRun--)
  {
    *data = (STRING::ToHexNibble(*b16Data) << 4) + STRING::ToHexNibble(*(b16Data + 1));
    b16Data += 2;
    ++data;
  }

  if (sz <= 6 || buffer[0] != 0 || buffer[1] != 0 || buffer[2] != 0 || buffer[3] != 1)
  {
    result = std::string(reinterpret_cast<const char*>(buffer), sz);
    return result;
  }

  data = buffer + 4;
  uint8_t* nalPos[4] = {data, nullptr, nullptr, nullptr};
  uint8_t* end = buffer + sz;

  while (data + 4 <= end && (data[0] != 0 || data[1] != 0 || data[2] != 0 || data[3] != 1))
  {
    ++data;
  }
  nalPos[1] = data += 4;

  while (data + 4 <= end && (data[0] != 0 || data[1] != 0 || data[2] != 0 || data[3] != 1))
  {
    ++data;
  }
  nalPos[2] = data += 4;

  // Check that we are at the end
  while (data + 4 <= end && (data[0] != 0 || data[1] != 0 || data[2] != 0 || data[3] != 1))
  {
    ++data;
  }

  if (data + 4 < end)
    return result;
  nalPos[3] = end + 4;

  // Check if we have expected information
  if (nalPos[0] < nalPos[1] && nalPos[1] < nalPos[2] && nalPos[2] < end && nalPos[0][0] == 0x40 &&
      nalPos[0][1] == 1 && nalPos[1][0] == 0x42 && nalPos[1][1] == 1 && nalPos[2][0] == 0x44 &&
      nalPos[2][1] == 1)
  {
    sz = 22 + sz - 12 + 16;
    result.resize(sz, 0); // Unknown HVCC fields
    data = reinterpret_cast<uint8_t*>(&result[22]);
    *data = 3, ++data; //numSequences;
    for (unsigned int i(0); i < 3; ++i)
    {
      *data = nalPos[i][0] >> 1, ++data; //Nalu type
      data[0] = 0, data[1] = 1, data += 2; //count nals
      uint16_t nalSz = static_cast<uint16_t>(nalPos[i + 1] - nalPos[i] - 4);
      data[0] = nalSz >> 8, data[1] = nalSz & 0xFF, data += 2; //count nals
      memcpy(data, nalPos[i], nalSz), data += nalSz;
    }
  }
  return result;
}

std::string UTILS::AnnexbToAvc(const char* b16Data)
{
  size_t sz = strlen(b16Data) >> 1;
  size_t szRun(sz);
  std::string result;

  if (sz > 1024)
    return result;

  uint8_t buffer[1024], *data(buffer);
  while (szRun--)
  {
    *data = (STRING::ToHexNibble(*b16Data) << 4) + STRING::ToHexNibble(*(b16Data + 1));
    b16Data += 2;
    ++data;
  }

  if (sz <= 6 || buffer[0] != 0 || buffer[1] != 0 || buffer[2] != 0 || buffer[3] != 1)
  {
    result = std::string(reinterpret_cast<const char*>(buffer), sz);
    return result;
  }

  uint8_t *sps = 0, *pps = 0, *end = buffer + sz;

  sps = pps = buffer + 4;

  while (pps + 4 <= end && (pps[0] != 0 || pps[1] != 0 || pps[2] != 0 || pps[3] != 1))
  {
    ++pps;
  }

  //Make sure we have found pps start
  if (pps + 4 >= end)
    return result;

  pps += 4;

  result.resize(sz + 3); //need 3 byte more for new header
  size_t pos(0);

  result[pos++] = 1;
  result[pos++] = static_cast<char>(sps[1]);
  result[pos++] = static_cast<char>(sps[2]);
  result[pos++] = static_cast<char>(sps[3]);
  result[pos++] =
      static_cast<char>(0xFFU); //6 bits reserved(111111) + 2 bits nal size length - 1 (11)
  result[pos++] = static_cast<char>(0xe1U); //3 bits reserved (111) + 5 bits number of sps (00001)

  sz = pps - sps - 4;
  result[pos++] = static_cast<const char>(sz >> 8);
  result[pos++] = static_cast<const char>(sz & 0xFF);
  result.replace(pos, sz, reinterpret_cast<const char*>(sps), sz);
  pos += sz;

  result[pos++] = 1;
  sz = end - pps;
  result[pos++] = static_cast<const char>(sz >> 8);
  result[pos++] = static_cast<const char>(sz & 0xFF);
  result.replace(pos, sz, reinterpret_cast<const char*>(pps), sz);
  pos += sz;

  return result;
}

std::string UTILS::AvcToAnnexb(const std::string& avc)
{
  if (avc.size() < 8)
    return "";

  // check if's already annexb, avc starts with 1
  if (avc[0] == 0)
    return avc;

  const uint8_t* avc_data(reinterpret_cast<const uint8_t*>(avc.data()));
  size_t avc_data_size(avc.size());

  // calculate size
  uint8_t buffer[1024];
  uint8_t buffer_size(4);
  buffer[0] = buffer[1] = buffer[2] = 0;
  buffer[3] = 1;

  //skip avc header
  avc_data += 6;
  avc_data_size -= 6;
  //sizeof SPS
  std::uint16_t sz(*avc_data);
  ++avc_data;
  --avc_data_size;
  sz = (sz << 8) | *avc_data;
  ++avc_data;
  --avc_data_size;
  //SPS
  memcpy(buffer + buffer_size, avc_data, sz);
  buffer_size += sz, avc_data_size -= sz, avc_data += sz;

  // Number PPS
  sz = *avc_data, ++avc_data, --avc_data_size;

  while (sz--)
  {
    buffer[buffer_size] = buffer[buffer_size + 1] = buffer[buffer_size + 2] = 0;
    buffer[buffer_size + 3] = 1;
    buffer_size += 4;
    std::uint16_t ppssz(*avc_data);
    ++avc_data;
    --avc_data_size;
    ppssz = (ppssz << 8) | *avc_data;
    ++avc_data;
    --avc_data_size;
    memcpy(buffer + buffer_size, avc_data, ppssz), buffer_size += ppssz, avc_data_size -= ppssz,
        avc_data += ppssz;
  }
  return std::string(reinterpret_cast<char*>(buffer), buffer_size);
}

std::string UTILS::ConvertKIDtoWVKID(std::string_view kid)
{
  std::string remapped;
  static const size_t remap[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
  for (size_t i{0}; i < 16; ++i)
  {
    remapped += kid[remap[i]];
  }
  return remapped;
}

std::string UTILS::ConvertKIDtoUUID(std::string_view kid)
{
  static char hexDigits[] = "0123456789abcdef";
  std::string uuid;
  for (size_t i{0}; i < 16; ++i)
  {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      uuid += '-';
    uuid += hexDigits[static_cast<uint8_t>(kid[i]) >> 4];
    uuid += hexDigits[static_cast<uint8_t>(kid[i]) & 15];
  }
  return uuid;
}

bool UTILS::CreateISMlicense(std::string_view key,
                             std::string_view licenseData,
                             std::vector<uint8_t>& initData)
{
  if (key.size() != 16 || licenseData.empty())
  {
    initData.clear();
    return false;
  }

  std::string decLicData = BASE64::Decode(licenseData);
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
  std::memcpy(protoptr, key.data(), 16);
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

    std::string uuidKid{ConvertKIDtoUUID(key)};
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

void UTILS::ParseHeaderString(std::map<std::string, std::string>& headerMap,
                              const std::string& header)
{
  std::vector<std::string> headers = STRING::SplitToVec(header, '&');
  for (std::string& header : headers)
  {
    size_t pos = header.find('=');
    if (pos != std::string::npos)
    {
      std::string value = header.substr(pos + 1);
      headerMap[header.substr(0, pos)] = STRING::URLDecode(StringUtils::Trim(value));
    }
  }
}

uint64_t UTILS::GetTimestamp()
{
  std::chrono::seconds unix_timestamp = std::chrono::seconds(std::time(NULL));
  using dCast = std::chrono::duration<std::uint64_t>;
  return std::chrono::duration_cast<dCast>(std::chrono::milliseconds(unix_timestamp)).count();
}

std::string UTILS::CODEC::FourCCToString(const uint32_t fourcc)
{
  std::string str;
  str += static_cast<char>((fourcc >> 24) & 255);
  str += static_cast<char>((fourcc >> 16) & 255);
  str += static_cast<char>((fourcc >> 8) & 255);
  str += static_cast<char>(fourcc & 255);
  return str;
}

bool UTILS::CODEC::Contains(const std::set<std::string>& list, std::string_view codec)
{
  return std::any_of(list.cbegin(), list.cend(),
                     [codec](const std::string& str) { return STRING::Contains(str, codec); });
}

bool UTILS::CODEC::Contains(const std::set<std::string>& list,
                            std::string_view codec,
                            std::string& codecStr)
{
  auto itCodec =
      std::find_if(list.cbegin(), list.cend(),
                   [codec](const std::string& str) { return STRING::Contains(str, codec); });
  if (itCodec != list.cend())
  {
    codecStr = *itCodec;
    return true;
  }
  codecStr.clear();
  return false;
}

std::string UTILS::CODEC::GetVideoDesc(const std::set<std::string>& list)
{
  for (const std::string& codec : list)
  {
    if (STRING::Contains(codec, FOURCC_AVC_) || STRING::Contains(codec, FOURCC_H264))
    {
      return "H.264";
    }
    if (STRING::Contains(codec, FOURCC_HEVC) || STRING::Contains(codec, FOURCC_HVC1) ||
        STRING::Contains(codec, FOURCC_DVH1) || STRING::Contains(codec, FOURCC_HEV1) ||
        STRING::Contains(codec, FOURCC_DVHE))
    {
      return "HEVC";
    }
    if (STRING::Contains(codec, FOURCC_VP09) || STRING::Contains(codec, NAME_VP9))
    {
      return "VP9";
    }
    if (STRING::Contains(codec, FOURCC_AV01) || STRING::Contains(codec, NAME_AV1))
    {
      return "AV1";
    }
  }
  return "";
}

bool UTILS::CODEC::IsAudio(std::string_view codec)
{
  for (const auto fourcc : CODEC::AUDIO_FOURCC_LIST)
  {
    if (STRING::Contains(codec, fourcc))
      return true;
  }
  for (const auto name : CODEC::AUDIO_NAME_LIST)
  {
    if (STRING::Contains(codec, name))
      return true;
  }
  return false;
}

bool UTILS::CODEC::IsVideo(std::string_view codec)
{
  for (const auto fourcc : CODEC::VIDEO_FOURCC_LIST)
  {
    if (STRING::Contains(codec, fourcc))
      return true;
  }
  for (const auto name : CODEC::VIDEO_NAME_LIST)
  {
    if (STRING::Contains(codec, name))
      return true;
  }
  return false;
}

bool UTILS::CODEC::IsSubtitleFourCC(std::string_view codec)
{
  for (const auto fourcc : CODEC::SUBTITLES_FOURCC_LIST)
  {
    if (STRING::Contains(codec, fourcc))
      return true;
  }
  return false;
}
