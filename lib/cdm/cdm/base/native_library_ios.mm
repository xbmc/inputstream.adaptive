/*
 *  Copyright (C) 2015 The Chromium Authors. All rights reserved.
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 *  See LICENSES/README.md for more information.
 */

#include "base/native_library.h"

#include "base/logging.h"

namespace base {

std::string NativeLibraryLoadError::ToString() const {
  return message;
}

// static
NativeLibrary LoadNativeLibrary(const base::FilePath& library_path,
                                NativeLibraryLoadError* error) {
  NOTIMPLEMENTED();
  if (error)
    error->message = "Not implemented.";
  return nullptr;
}

// static
void UnloadNativeLibrary(NativeLibrary library) {
  NOTIMPLEMENTED();
  DCHECK(!library);
}

// static
void* GetFunctionPointerFromNativeLibrary(NativeLibrary library,
                                          const char* name) {
  NOTIMPLEMENTED();
  return nullptr;
}

// static
string16 GetNativeLibraryName(const string16& name) {
  return name;
}

}  // namespace base
