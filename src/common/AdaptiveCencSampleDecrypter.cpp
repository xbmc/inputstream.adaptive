/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AdaptiveCencSampleDecrypter.h"

CAdaptiveCencSampleDecrypter::CAdaptiveCencSampleDecrypter(
    std::shared_ptr<Adaptive_CencSingleSampleDecrypter> singleSampleDecrypter,
    AP4_CencSampleInfoTable* sampleInfoTable)
  : AP4_CencSampleDecrypter(singleSampleDecrypter.get(), sampleInfoTable)
{
  m_decrypter = singleSampleDecrypter;
}

AP4_Result CAdaptiveCencSampleDecrypter::DecryptSampleData(AP4_UI32 poolid,
                                       AP4_DataBuffer& data_in,
                                       AP4_DataBuffer& data_out,
                                       const AP4_UI08* iv)
  {
    // increment the sample cursor
    unsigned int sample_cursor = m_SampleCursor++;

    // setup the IV
    unsigned char iv_block[16];
    if (!iv)
    {
      iv = m_SampleInfoTable->GetIv(sample_cursor);
    }
    if (!iv)
      return AP4_ERROR_INVALID_FORMAT;
    unsigned int iv_size = m_SampleInfoTable->GetIvSize();
    AP4_CopyMemory(iv_block, iv, iv_size);
    if (iv_size != 16)
      AP4_SetMemory(&iv_block[iv_size], 0, 16 - iv_size);

    // get the subsample info for this sample if needed
    unsigned int subsample_count = 0;
    const AP4_UI16* bytes_of_cleartext_data = nullptr;
    const AP4_UI32* bytes_of_encrypted_data = nullptr;
    if (m_SampleInfoTable)
    {
      AP4_Result result = m_SampleInfoTable->GetSampleInfo(
          sample_cursor, subsample_count, bytes_of_cleartext_data, bytes_of_encrypted_data);
      if (AP4_FAILED(result))
        return result;
    }

    // decrypt the sample
    return m_decrypter->DecryptSampleData(poolid, data_in, data_out, iv_block, subsample_count,
                                          bytes_of_cleartext_data, bytes_of_encrypted_data);
  }
