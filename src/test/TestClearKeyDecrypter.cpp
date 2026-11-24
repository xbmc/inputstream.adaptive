/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "decrypters/clearkey/ClearKeyCencSingleSampleDecrypter.h"

#include <gtest/gtest.h>

TEST(ClearKeyDecrypterTest, NewPoolDoesNotReuseAnActiveId)
{
  CClearKeyCencSingleSampleDecrypter decrypter({}, nullptr);
  const AP4_UI32 removedPool = decrypter.AddPool();
  const AP4_UI32 activePool = decrypter.AddPool();
  ASSERT_NE(removedPool, activePool);

  decrypter.RemovePool(removedPool);

  EXPECT_NE(decrypter.AddPool(), activePool);
}
