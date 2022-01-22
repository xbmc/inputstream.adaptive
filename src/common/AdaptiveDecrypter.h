/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include <bento4/Ap4.h>

enum class ENCRYPTION_SCHEME
{
  NONE,
  CENC,
  CBCS
};

class Adaptive_CencSingleSampleDecrypter : public AP4_CencSingleSampleDecrypter
{
public:
  Adaptive_CencSingleSampleDecrypter() : AP4_CencSingleSampleDecrypter(0){};

  void SetCrypto(AP4_UI08 cryptBlocks, AP4_UI08 skipBlocks)
  {
    m_CryptBlocks = static_cast<uint32_t>(cryptBlocks);
    m_SkipBlocks = static_cast<uint32_t>(skipBlocks);
  };
  virtual void SetEncryptionScheme(ENCRYPTION_SCHEME encryptionScheme){};

protected:
  uint32_t m_CryptBlocks{0};
  uint32_t m_SkipBlocks{0};
};
