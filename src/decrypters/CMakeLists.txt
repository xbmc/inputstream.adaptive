set(SOURCES
  DrmFactory.cpp
  Helpers.cpp
)

set(HEADERS
  DrmFactory.h
  Helpers.h
  IDecrypter.h
)

add_dir_sources(SOURCES HEADERS)

if(NOT CORE_SYSTEM_NAME STREQUAL ios AND NOT CORE_SYSTEM_NAME STREQUAL darwin_embedded)
  if(CORE_SYSTEM_NAME STREQUAL android)
    add_subdirectory(widevineandroid)
  else()
    add_subdirectory(widevine)
  endif()
endif()

if(CORE_SYSTEM_NAME STREQUAL windows)
	include(CheckCXXSourceCompiles)
	check_cxx_source_compiles([=[
		#include <sdkddkver.h>
		static_assert(WDK_NTDDI_VERSION >= NTDDI_WIN10_VB, "Inspecting WDK_NTDDI_VERSION, the Windows SDK version.");
		int main() {}]=] WINDOWS_SDK_VERSION_CHECK)

	if(NOT WINDOWS_SDK_VERSION_CHECK)
		message(WARNING "MediaFoundation (for Playready) is only available with Windows 10 SDK (10.0.19041) or later.")
	else()
		add_subdirectory(mediafoundation)
	endif()
endif()
