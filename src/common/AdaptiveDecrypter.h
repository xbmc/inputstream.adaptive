/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../CryptoMode.h"

#include <stdexcept>
#include <string_view>

#include <bento4/Ap4.h>

class Adaptive_CencSingleSampleDecrypter : public AP4_CencSingleSampleDecrypter
{
public:
  Adaptive_CencSingleSampleDecrypter() : AP4_CencSingleSampleDecrypter(0){};

  void SetCrypto(AP4_UI08 cryptBlocks, AP4_UI08 skipBlocks)
  {
    m_CryptBlocks = static_cast<uint32_t>(cryptBlocks);
    m_SkipBlocks = static_cast<uint32_t>(skipBlocks);
  };
  virtual void SetEncryptionMode(CryptoMode encryptionMode){};

  /*! \brief Add a Key ID to the current session
   *  \param keyId The KID
   */
  virtual void AddKeyId(std::string_view keyId)
  {
    throw std::logic_error("AddKeyId method not implemented.");
  };

  /*! \brief Set a Key ID as default
   *  \param keyId The KID
   */
  virtual void SetDefaultKeyId(std::string_view keyId)
  {
    throw std::logic_error("SetDefaultKeyId method not implemented.");
  };

protected:
  uint32_t m_CryptBlocks{0};
  uint32_t m_SkipBlocks{0};
};
