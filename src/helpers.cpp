/*
*      Copyright (C) 2016-2016 peak3d
*      http://www.peak3d.de
*
*  This Program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This Program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  <http://www.gnu.org/licenses/>.
*
*/

#include "helpers.h"
#include <cstring>
#include "oscompat.h"
#include <stdlib.h>
#include <sstream>

#ifndef BYTE
typedef unsigned char BYTE;
#endif

std::string ToDecimal(const uint8_t *data, size_t data_size)
{
  std::stringstream ret;

  if (data_size)
    ret << static_cast<unsigned int>(data[0]);

  for (size_t i(1); i < data_size; ++i)
    ret << ','  << static_cast<unsigned int>(data[i]);

  return ret.str();
}

static const BYTE from_base64[] = { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 62, 255, 62, 255, 63,
52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 255, 255, 0, 255, 255, 255,
255, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 255, 255, 255, 255, 63,
255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 255, 255, 255, 255, 255 };


bool b64_decode(const char *in, unsigned int in_len, uint8_t *out, unsigned int &out_len)
{
  // Make sure string length is a multiple of 4
  char *in_copy(0);
  if (in_len > 3 && strnicmp(in + (in_len - 3), "%3D", 3) == 0)
  {
    in_copy = (char *)malloc(in_len + 1);
    strcpy(in_copy, in);
    in = in_copy;
    if (in_len > 6 && strnicmp(in + (in_len - 6), "%3D", 3) == 0)
    {
      strcpy(in_copy + (in_len - 6), "==");
      in_len -= 4;
    }
    else {
      strcpy(in_copy + (in_len - 3), "=");
      in_len -= 2;
    }
  }
  else if (in_len <= 3)
  {
    out_len = 0;
    return false;
  }

  if (strchr(in, '\\') != 0)
  {
    if (!in_copy)
    {
      in_copy = (char *)malloc(in_len + 1);
      memcpy(in_copy, in, in_len);
      in = in_copy;
    }
    char *run(in_copy);
    for (size_t i(0); i < in_len; ++i)
      if (in_copy[i] != '\\')
        *run++ = in_copy[i];
    in_len = run - in_copy;
  }

  if (in_len & 3)
  {
    free(in_copy);
    out_len = 0;
    return false;
  }

  unsigned int new_out_len = in_len / 4 * 3;
  if (in[in_len - 1] == '=') --new_out_len;
  if (in[in_len - 2] == '=') --new_out_len;
  if (new_out_len > out_len)
  {
    free(in_copy);
        out_len = 0;
    return false;
  }
  out_len = new_out_len;

  for (size_t i = 0; i < in_len; i += 4)
  {
    // Get values for each group of four base 64 characters
    BYTE b4[4];
    b4[0] = (in[i + 0] <= 'z') ? from_base64[static_cast<uint8_t>(in[i + 0])] : 0xff;
    b4[1] = (in[i + 1] <= 'z') ? from_base64[static_cast<uint8_t>(in[i + 1])] : 0xff;
    b4[2] = (in[i + 2] <= 'z') ? from_base64[static_cast<uint8_t>(in[i + 2])] : 0xff;
    b4[3] = (in[i + 3] <= 'z') ? from_base64[static_cast<uint8_t>(in[i + 3])] : 0xff;

    // Transform into a group of three bytes
    BYTE b3[3];
    b3[0] = ((b4[0] & 0x3f) << 2) + ((b4[1] & 0x30) >> 4);
    b3[1] = ((b4[1] & 0x0f) << 4) + ((b4[2] & 0x3c) >> 2);
    b3[2] = ((b4[2] & 0x03) << 6) + ((b4[3] & 0x3f) >> 0);

    // Add the byte to the return value if it isn't part of an '=' character (indicated by 0xff)
    if (b4[1] != 0xff) *out++ = b3[0];
    if (b4[2] != 0xff) *out++ = b3[1];
    if (b4[3] != 0xff) *out++ = b3[2];
  }
  free(in_copy);
  return true;
}

static const char *to_base64 =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ\
abcdefghijklmnopqrstuvwxyz\
0123456789+/";

std::string b64_encode(unsigned char const* in, unsigned int in_len, bool urlEncode)
{
  std::string ret;
  int i(3);
  unsigned char c_3[3];
  unsigned char c_4[4];

  while (in_len) {
    i = in_len > 2 ? 3 : in_len;
    in_len -= i;
    c_3[0] = *(in++);
    c_3[1] = i > 1 ? *(in++) : 0;
    c_3[2] = i > 2 ? *(in++) : 0;

    c_4[0] = (c_3[0] & 0xfc) >> 2;
    c_4[1] = ((c_3[0] & 0x03) << 4) + ((c_3[1] & 0xf0) >> 4);
    c_4[2] = ((c_3[1] & 0x0f) << 2) + ((c_3[2] & 0xc0) >> 6);
    c_4[3] = c_3[2] & 0x3f;

    for (int j = 0; (j < i + 1); ++j)
    {
      if (urlEncode && to_base64[c_4[j]] == '+')
        ret += "%2B";
      else if (urlEncode && to_base64[c_4[j]] == '/')
        ret += "%2F";
      else
        ret += to_base64[c_4[j]];
    }
  }
  while ((i++ < 3))
    ret += urlEncode ? "%3D" : "=";
  return ret;
}

bool replace(std::string& s, const std::string& from, const std::string& to)
{
  size_t start_pos = s.find(from);
  if (start_pos == std::string::npos)
    return false;
  s.replace(start_pos, from.length(), to);
  return true;
}

void replaceAll(std::string& s, const std::string& from, const std::string& to, bool nextEmpty)
{
  if (from.empty())
    return;
  size_t pos = 0;
  bool isFirstReplaced = false;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    if (isFirstReplaced)
    {
      s.replace(pos, from.length(), "");
    }
    else
    {
      s.replace(pos, from.length(), to);
      pos += to.length();
      if (nextEmpty)
          isFirstReplaced = true;
    }
  }
}

std::vector<std::string> split(const std::string& s, char seperator)
{
  std::vector<std::string> output;
  std::string::size_type prev_pos = 0, pos = 0;

  while ((pos = s.find(seperator, pos)) != std::string::npos)
  {
    std::string substring(s.substr(prev_pos, pos - prev_pos));
    output.push_back(substring);
    prev_pos = ++pos;
  }
  output.push_back(s.substr(prev_pos, pos - prev_pos));
  return output;
}

std::string &trim(std::string &src)
{
  src.erase(0, src.find_first_not_of(" "));
  src.erase(src.find_last_not_of(" ") + 1);
  return src;
}

static std::string trimcp(std::string src)
{
  src.erase(0, src.find_first_not_of(" "));
  src.erase(src.find_last_not_of(" ") + 1);
  return src;
}

static char from_hex(char ch) {
  return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

std::string url_decode(std::string text) {
  char h;
  std::string escaped;

  for (auto i = text.begin(), n = text.end(); i != n; ++i) {
    std::string::value_type c = (*i);
    if (c == '%' && (n - i) >= 3)
    {
      if (i[1] && i[2]) {
        h = from_hex(i[1]) << 4 | from_hex(i[2]);
        escaped += h;
        i += 2;
      }
    }
    else if (c == '+')
      escaped += ' ';
    else {
      escaped += c;
    }
  }
  return escaped;
}

unsigned char HexNibble(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  else if (c >= 'a' && c <= 'f')
    return 10 + (c - 'a');
  else if (c >= 'A' && c <= 'F')
    return 10 + (c - 'A');
  return 0;
}

std::string annexb_to_hvcc(const char* b16_data)
{
  unsigned int sz = strlen(b16_data) >> 1, szRun(sz);
  std::string result;

  if (sz > 1024)
    return result;

  uint8_t buffer[1024], *data(buffer);
  while (szRun--)
  {
    *data = (HexNibble(*b16_data) << 4) + HexNibble(*(b16_data + 1));
    b16_data += 2;
    ++data;
  }

  if (sz <= 6 || buffer[0] != 0 || buffer[1] != 0 || buffer[2] != 0 || buffer[3] != 1)
  {
    result = std::string((const char*)buffer, sz);
    return result;
  }

  data = buffer + 4;
  uint8_t* nalPos[4] = {data, nullptr, nullptr, nullptr};
  uint8_t* end = buffer + sz;

  while (data + 4 <= end && (data[0] != 0 || data[1] != 0 || data[2] != 0 || data[3] != 1))
    ++data;
  nalPos[1] = data += 4;

  while (data + 4 <= end && (data[0] != 0 || data[1] != 0 || data[2] != 0 || data[3] != 1))
    ++data;
  nalPos[2] = data += 4;

  // Check that we are at the end
  while (data + 4 <= end && (data[0] != 0 || data[1] != 0 || data[2] != 0 || data[3] != 1))
    ++data;

  if (data + 4 < end)
    return result;
  nalPos[3] = end + 4;

  // Check if we have expected information
  if (nalPos[0] < nalPos[1] && nalPos[1] < nalPos[2] && nalPos[2] < end  &&
    nalPos[0][0] == 0x40 && nalPos[0][1] == 1 && nalPos[1][0] == 0x42 &&
    nalPos[1][1] == 1 && nalPos[2][0] == 0x44 && nalPos[2][1] == 1)
  {
    sz = 22 + sz - 12 + 16;
    result.resize(sz, 0); // Unknown HVCC fields
    data = reinterpret_cast<uint8_t*>(&result[22]);
    *data = 3, ++data; //numSequences;
    for (unsigned int i(0); i < 3; ++i)
    {
      *data = nalPos[i][0] >> 1, ++data; //Nalu type
      data[0] = 0, data[1] = 1, data += 2; //count nals
      uint16_t nalSz = nalPos[i + 1] - nalPos[i] - 4;
      data[0] = nalSz >> 8, data[1] = nalSz & 0xFF, data += 2; //count nals
      memcpy(data, nalPos[i], nalSz), data += nalSz;
    }
  }
  return result;
}

std::string annexb_to_avc(const char *b16_data)
{
  unsigned int sz = strlen(b16_data) >> 1, szRun(sz);
  std::string result;

  if (sz > 1024)
    return result;

  uint8_t buffer[1024], *data(buffer);
  while (szRun--)
  {
    *data = (HexNibble(*b16_data) << 4) + HexNibble(*(b16_data+1));
    b16_data += 2;
    ++data;
  }

  if (sz <= 6 || buffer[0] != 0 || buffer[1] != 0 || buffer[2] != 0 || buffer[3] != 1)
  {
    result = std::string((const char*)buffer, sz);
    return result;
  }

  uint8_t *sps = 0, *pps = 0, *end = buffer + sz;

  sps = pps = buffer + 4;

  while (pps + 4 <= end && (pps[0] != 0 || pps[1] != 0 || pps[2] != 0 || pps[3] != 1))
    ++pps;

  //Make sure we have found pps start
  if (pps + 4 >= end)
    return result;

  pps += 4;

  result.resize(sz + 3); //need 3 byte more for new header
  unsigned int pos(0);

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
  result.replace(pos, sz, (const char*)sps, sz); pos += sz;

  result[pos++] = 1;
  sz = end - pps;
  result[pos++] = static_cast<const char>(sz >> 8);
  result[pos++] = static_cast<const char>(sz & 0xFF);
  result.replace(pos, sz, (const char*)pps, sz); pos += sz;

  return result;
}

std::string avc_to_annexb(const std::string &avc)
{
  if (avc.size() < 8)
    return "";

  // check if's already annexb, avc starts with 1
  if (avc[0] == 0)
    return avc;

  const uint8_t *avc_data(reinterpret_cast<const uint8_t*>(avc.data()));
  size_t avc_data_size(avc.size());

  // calculate size
  uint8_t buffer[1024];
  uint8_t buffer_size(4);
  buffer[0] = buffer[1] = buffer[2] = 0; buffer[3] = 1;

  //skip avc header
  avc_data += 6; avc_data_size -= 6;
  //sizeof SPS
  std::uint16_t sz(*avc_data); ++avc_data; --avc_data_size;
  sz = (sz << 8) | *avc_data; ++avc_data; --avc_data_size;
  //SPS
  memcpy(buffer + buffer_size, avc_data, sz);
  buffer_size += sz, avc_data_size -= sz, avc_data += sz;

  // Number PPS
  sz = *avc_data, ++avc_data, --avc_data_size;

  while (sz--)
  {
    buffer[buffer_size] = buffer[buffer_size + 1] = buffer[buffer_size + 2] = 0; buffer[buffer_size + 3] = 1; buffer_size += 4;
    std::uint16_t ppssz(*avc_data); ++avc_data; --avc_data_size;
    ppssz = (ppssz << 8) | *avc_data; ++avc_data; --avc_data_size;
    memcpy(buffer + buffer_size, avc_data, ppssz), buffer_size += ppssz, avc_data_size -= ppssz, avc_data += ppssz;
  }
  return std::string(reinterpret_cast<char*>(buffer), buffer_size);
}


void prkid2wvkid(const char *input, char *output)
{
  static const uint8_t remap[16] = { 3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15 };
  for (unsigned int i(0); i < 16; ++i)
    output[i] = input[remap[i]];
}

char* KIDtoUUID(const uint8_t* kid, char* dst)
{
  static const uint8_t hexmap[16] = { '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f' };
  for (unsigned int i(0); i < 16; ++i)
  {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      *dst++ = '-';
    *dst++ = hexmap[(uint8_t)(kid[i]) >> 4];
    *dst++ = hexmap[(uint8_t)(kid[i]) & 15];
  }
  return dst;
}

bool create_ism_license(std::string key, std::string license_data, std::vector<uint8_t>& init_data)
{
  if (key.size() != 16 || license_data.empty())
  {
    init_data.clear();
    return false;
  }

  uint8_t ld[1024];
  unsigned int ld_size(1024);
  b64_decode(license_data.c_str(), license_data.size(), ld, ld_size);
  ld[ld_size] = 0;

  const uint8_t *kid((uint8_t*)strstr((const char*)ld, "{KID}"));
  const uint8_t *uuid((uint8_t*)strstr((const char*)ld, "{UUID}"));
  unsigned int license_size = uuid ? ld_size + 36 - 6 : ld_size;

  //Build up proto header
  init_data.resize(512);
  uint8_t* protoptr(init_data.data());
  if (kid)
  {
    if (uuid && uuid < kid)
      return false;
    license_size -= 5; //Remove sizeof(placeholder)
    memcpy(protoptr, ld, kid - ld);
    protoptr += kid - ld;
    license_size -= kid - ld;
    kid += 5;
    ld_size -= kid - ld;
  }
  else
    kid = ld;

  *protoptr++ = 18; //id=16>>3=2, type=2(flexlen)
  *protoptr++ = 16; //length of key
  memcpy(protoptr, key.data(), 16);
  protoptr += 16;
  //-----------
  *protoptr++ = 34;//id=32>>3=4, type=2(flexlen)
  do {
    *protoptr++ = static_cast<uint8_t>(license_size & 127);
    license_size >>= 7;
    if (license_size)
      *(protoptr - 1) |= 128;
    else
      break;
  } while (1);
  if (uuid)
  {
    memcpy(protoptr, kid, uuid - kid);
    protoptr += uuid - kid;

    protoptr = reinterpret_cast<uint8_t*>(KIDtoUUID((const uint8_t*)key.data(), reinterpret_cast<char*>(protoptr)));

    unsigned int sizeleft = ld_size - ((uuid - kid) + 6);
    memcpy(protoptr, uuid + 6, sizeleft);
    protoptr += sizeleft;
  }
  else
  {
    memcpy(protoptr, kid, ld_size);
    protoptr += ld_size;
  }
  init_data.resize(protoptr - init_data.data());

  return true;
}

void parseheader(std::map<std::string, std::string>& headerMap, const std::string& headerString)
{
  std::vector<std::string> headers = split(headerString, '&');
  for (std::vector<std::string>::iterator b(headers.begin()), e(headers.end()); b != e; ++b)
  {
    std::string::size_type pos(b->find('='));
    if(pos != std::string::npos)
      headerMap[trimcp(b->substr(0, pos))] = url_decode(trimcp(b->substr(pos+1)));
  }
}

int endswith(const char* in, const char* suffix)
{
  int l1 = strlen(suffix);
  int l2 = strlen(in);
  if (l1 > l2)
    return 0;

  return strcmp(suffix, in + (l2 - l1)) == 0;
}
