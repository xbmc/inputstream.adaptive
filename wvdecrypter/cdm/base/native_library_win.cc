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

#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
typedef HMODULE (WINAPI* LoadLibraryFunction)(const wchar_t* file_name, unsigned long res);
#else
typedef HMODULE (WINAPI* LoadLibraryFunction)(const wchar_t* file_name);
#endif

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
  std::wstring lp;
  std::wstring plugin_path, plugin_value;

  int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, library_path.c_str(), (int)library_path.length(), NULL, 0);
  if (len)
  {
    lp.resize(len);
    len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, library_path.c_str(), (int)library_path.length(), &lp[0], len);
  }
  if (!len)
    lp.assign(library_path.begin(), library_path.end());

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

#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
  HMODULE module = (*load_library_api)((wchar_t*)plugin_value.c_str(), 0);
#else
  HMODULE module = (*load_library_api)((wchar_t*)plugin_value.c_str());
#endif
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
#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
  return LoadNativeLibraryHelper(library_path, LoadPackagedLibrary, error);
#else
  return LoadNativeLibraryHelper(library_path, LoadLibraryW, error);
#endif
}

NativeLibrary LoadNativeLibraryDynamically(const std::string& library_path) {
  typedef HMODULE (WINAPI* LoadLibraryFunction)(const wchar_t* file_name);

#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
  return LoadNativeLibraryHelper(library_path, LoadPackagedLibrary, NULL);
#else
  LoadLibraryFunction load_library;
  load_library = reinterpret_cast<LoadLibraryFunction>(
      GetProcAddress(GetModuleHandle(L"kernel32.dll"), "LoadLibraryW"));

  return LoadNativeLibraryHelper(library_path, load_library, NULL);
#endif
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
