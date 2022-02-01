/*
 *  Copyright (C) 2015 The Chromium Authors. All rights reserved.
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 *  See LICENSES/README.md for more information.
 */

#include "base/files/file_path.h"
#include "base/native_library.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

const FilePath::CharType kDummyLibraryPath[] =
    FILE_PATH_LITERAL("dummy_library");

TEST(NativeLibraryTest, LoadFailure) {
  NativeLibraryLoadError error;
  NativeLibrary library =
      LoadNativeLibrary(FilePath(kDummyLibraryPath), &error);
  EXPECT_TRUE(library == nullptr);
  EXPECT_FALSE(error.ToString().empty());
}

// |error| is optional and can be null.
TEST(NativeLibraryTest, LoadFailureWithNullError) {
  NativeLibrary library =
      LoadNativeLibrary(FilePath(kDummyLibraryPath), nullptr);
  EXPECT_TRUE(library == nullptr);
}

}  // namespace base
