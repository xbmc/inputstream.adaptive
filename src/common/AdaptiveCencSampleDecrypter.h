/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AdaptiveDecrypter.h"

#include <bento4/Ap4.h>

class CAdaptiveCencSampleDecrypter : public AP4_CencSampleDecrypter
{
public:
  CAdaptiveCencSampleDecrypter(Adaptive_CencSingleSampleDecrypter* singleSampleDecrypter,
                               AP4_CencSampleInfoTable* sampleInfoTable);

  virtual AP4_Result DecryptSampleData(AP4_UI32 poolid,
                                       AP4_DataBuffer& data_in,
                                       AP4_DataBuffer& data_out,
                                       const AP4_UI08* iv);

protected:
  Adaptive_CencSingleSampleDecrypter* m_decrypter;
};
