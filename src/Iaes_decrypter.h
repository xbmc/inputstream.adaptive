/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <bento4/Ap4Types.h>

#include <cstdint>
#include <string>
#include <vector>

class IAESDecrypter
{
public:
  virtual ~IAESDecrypter() {};

  virtual void decrypt(const AP4_UI08* aes_key,
                       const AP4_UI08* aes_iv,
                       const AP4_UI08* src,
                       std::vector<uint8_t>& dst,
                       size_t dstOffset,
                       size_t& dataSize,
                       bool lastChunk) = 0;
  virtual std::vector<uint8_t> convertIV(const std::string& input) = 0;
  virtual void ivFromSequence(uint8_t* buffer, uint64_t sid) = 0;
  // virtual const std::string& getLicenseKey() const = 0;
  // virtual bool RenewLicense(const std::string& pluginUrl) = 0;
};
