/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "Iaes_decrypter.h"

#include <bento4/Ap4Types.h>

#include <string>
#include <vector>

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

class ATTR_DLL_LOCAL AESDecrypter : public IAESDecrypter
{
public:
  AESDecrypter() = default;
  // AESDecrypter(std::string_view licenseKey) : m_licenseKey(licenseKey) {}
  virtual ~AESDecrypter() = default;

  void decrypt(const AP4_UI08* aes_key,
               const AP4_UI08* aes_iv,
               const AP4_UI08* src,
               std::vector<uint8_t>& dst,
               size_t dstOffset,
               size_t& dataSize,
               bool lastChunk);
  std::vector<uint8_t> convertIV(const std::string& input);
  void ivFromSequence(uint8_t* buffer, uint64_t sid);
  // const std::string& getLicenseKey() const { return m_licenseKey; };
  // bool RenewLicense(const std::string& pluginUrl);

  // private:
  //   std::string m_licenseKey;
};
