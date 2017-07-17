// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "native_library.h"

#include <windows.h>

//#include "base/files/file_util.h"
//#include "base/strings/stringprintf.h"
//#include "base/strings/utf_string_conversions.h"
//#include "base/threading/thread_restrictions.h"

namespace base {

typedef HMODULE (WINAPI* LoadLibraryFunction)(const wchar_t* file_name);

namespace {

NativeLibrary LoadNativeLibraryHelper(const std::string& library_path,
                                      LoadLibraryFunction load_library_api,
                                      NativeLibraryLoadError* error) {
  // LoadLibrary() opens the file off disk.
  //ThreadRestrictions::AssertIOAllowed();

  // Switch the current directory to the library directory as the library
  // may have dependencies on DLLs in this directory.
  bool restore_directory = false;
  wchar_t current_directory[MAX_PATH];
  std::wstring lp = std::wstring(library_path.begin(), library_path.end());
  std::wstring plugin_path, plugin_value;
  if (GetCurrentDirectory(MAX_PATH,current_directory))
  {
	const wchar_t *res = wcsrchr(lp.c_str(), '\\');
	if (res)
	{
	  plugin_path.assign(lp.c_str(),res);
	  plugin_value.assign(++res, wcsrchr(res,0));
	}
	else
	  plugin_value = lp;

	if (!plugin_path.empty())
	{
      SetCurrentDirectory((wchar_t*)plugin_path.c_str());
      restore_directory = true;
    }
  }

  HMODULE module = (*load_library_api)((wchar_t*)plugin_value.c_str());
  if (!module && error) {
    // GetLastError() needs to be called immediately after |load_library_api|.
    error->code = GetLastError();
  }

  if (restore_directory)
    SetCurrentDirectory(current_directory);

  return module;
}

}  // namespace

std::string NativeLibraryLoadError::ToString() const
{
	char buf[32];
	return int2char(code, buf);
}

// static
NativeLibrary LoadNativeLibrary(const std::string& library_path,
                                NativeLibraryLoadError* error) {
  return LoadNativeLibraryHelper(library_path, LoadLibraryW, error);
}

NativeLibrary LoadNativeLibraryDynamically(const std::string& library_path) {
  typedef HMODULE (WINAPI* LoadLibraryFunction)(const wchar_t* file_name);

  LoadLibraryFunction load_library;
  load_library = reinterpret_cast<LoadLibraryFunction>(
      GetProcAddress(GetModuleHandle(L"kernel32.dll"), "LoadLibraryW"));

  return LoadNativeLibraryHelper(library_path, load_library, NULL);
}

// static
void UnloadNativeLibrary(NativeLibrary library) {
  FreeLibrary(library);
}

// static
void* GetFunctionPointerFromNativeLibrary(NativeLibrary library,
                                          const char* name) {
  return GetProcAddress(library, name);
}

// static
//string16 GetNativeLibraryName(const string16& name) {
//  return name + ASCIIToUTF16(".dll");
//}

}  // namespace base
