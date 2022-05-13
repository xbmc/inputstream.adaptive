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

#pragma once

#include <string>
#include <stdint.h>
#include <vector>
#include <map>

bool b64_decode(const char *in, unsigned int in_len, uint8_t *out, unsigned int &out_len);

std::string ToDecimal(const uint8_t *data, size_t data_size);
std::string b64_encode(unsigned char const* in, unsigned int in_len, bool urlEncode);

bool replace(std::string& s, const std::string& from, const std::string& to);
void replaceAll(std::string& s, const std::string& from, const std::string& to, bool nextEmpty);

std::vector<std::string> split(const std::string& s, char seperator);

std::string &trim(std::string &src);

std::string url_decode(std::string text);

std::string annexb_to_hvcc(const char* b16_data);
std::string annexb_to_avc(const char *b16_data);
std::string avc_to_annexb(const std::string &avc);

unsigned char HexNibble(char c);

void prkid2wvkid(const char *input, char *output);
char* KIDtoUUID(const uint8_t* kid, char* dst);
bool create_ism_license(std::string key, std::string license_data, std::vector<uint8_t>& init_data);
void parseheader(std::map<std::string, std::string>& headerMap, const std::string& headerString);
int endswith(const char* in, const char* suffix);

#define MKTAG(a,b,c,d) ((a) | ((b) << 8) | ((c) << 16) | ((unsigned)(d) << 24))
