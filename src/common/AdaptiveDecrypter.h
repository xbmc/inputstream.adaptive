/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../utils/CryptoUtils.h"

#include <stdexcept>
#include <string_view>

#include <bento4/Ap4.h>

class Adaptive_CencSingleSampleDecrypter : public AP4_CencSingleSampleDecrypter
{
public:
  Adaptive_CencSingleSampleDecrypter() : AP4_CencSingleSampleDecrypter(0){};

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

  virtual AP4_Result SetFragmentInfo(AP4_UI32 pool_id,
                                     const AP4_UI08* key,
                                     const AP4_UI08 nal_length_size,
                                     AP4_DataBuffer& annexb_sps_pps,
                                     AP4_UI32 flags,
                                     CryptoInfo cryptoInfo) = 0;

  virtual AP4_Result DecryptSampleData(AP4_UI32 poolid,
                                       AP4_DataBuffer& data_in,
                                       AP4_DataBuffer& data_out,
                                       const AP4_UI08* iv,
                                       unsigned int subsample_count,
                                       const AP4_UI16* bytes_of_cleartext_data,
                                       const AP4_UI32* bytes_of_encrypted_data) = 0;

  virtual AP4_UI32 AddPool() { return 0; }
  virtual void RemovePool(AP4_UI32 poolid) {}
  virtual const char* GetSessionId() { return nullptr; }
};
