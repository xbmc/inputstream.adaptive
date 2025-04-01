/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

 // These values must match their respective constant values
 // defined in the Android MediaCodec class
enum class CryptoMode
{
  NONE = 0,
  AES_CTR = 1,
  AES_CBC = 2
};

struct CryptoInfo
{
  uint8_t m_cryptBlocks{0};
  uint8_t m_skipBlocks{0};
  CryptoMode m_mode{ CryptoMode::NONE };
};

// \brief AES-128 Key info
struct CAesKeyInfo
{
  std::vector<uint8_t> key;
  std::vector<uint8_t> iv;
  std::string keyUrl;
};
