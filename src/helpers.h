/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
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
