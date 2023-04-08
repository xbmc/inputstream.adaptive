/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include <bento4/Ap4.h>

class CAdaptiveCencSampleDecrypter : public AP4_CencSampleDecrypter
{
public:
  CAdaptiveCencSampleDecrypter(AP4_CencSingleSampleDecrypter* single_sample_decrypter,
                               AP4_CencSampleInfoTable* sample_info_table)
    : AP4_CencSampleDecrypter(single_sample_decrypter, sample_info_table)
  {
  }

  virtual AP4_Result DecryptSampleData(AP4_UI32 poolid,
    AP4_DataBuffer& data_in,
    AP4_DataBuffer& data_out,
    const AP4_UI08* iv);
};
