/*
 *  Copyright (C) 2017 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "aes_decrypter.h"
#include <bento4/Ap4Protection.h>
#include <kodi/Filesystem.h>
#include <vector>

void AESDecrypter::decrypt(const AP4_UI08 *aes_key, const AP4_UI08 *aes_iv, const AP4_UI08 *src, AP4_UI08 *dst, size_t dataSize)
{
  AP4_BlockCipher* cbc_d_block_cipher;
  AP4_DefaultBlockCipherFactory::Instance.CreateCipher(
    AP4_BlockCipher::AES_128,
    AP4_BlockCipher::DECRYPT,
    AP4_BlockCipher::CBC,
    NULL,
    aes_key,
    16,
    cbc_d_block_cipher);

  cbc_d_block_cipher->Process(src, dataSize, dst, aes_iv);

  delete cbc_d_block_cipher;
}

std::string AESDecrypter::convertIV(const std::string &input)
{
  std::string result;
  result.resize(16);
  AP4_Result ret(AP4_ERROR_INVALID_FORMAT);

  if (input.size() == 34)
    ret = AP4_ParseHex(input.c_str() + 2, reinterpret_cast<unsigned char*>(&result[0]), 16);
  else if (input.size() == 32)
    ret = AP4_ParseHex(input.c_str(), reinterpret_cast<unsigned char*>(&result[0]), 16);
  if (!AP4_SUCCEEDED(ret))
    result.clear();
  return result;
}

void AESDecrypter::ivFromSequence(uint8_t *buffer, uint64_t sid)
{
  memset(buffer, 0, 16);
  AP4_BytesFromUInt64BE(buffer + 8, sid);
}

bool AESDecrypter::RenewLicense(const std::string &pluginUrl)
{
  std::vector<kodi::vfs::CDirEntry> items;
  if (kodi::vfs::GetDirectory(pluginUrl, "", items) && items.size() == 1)
  {
    m_licenseKey = items[0].Path();
    return true;
  }
  return false;
}
