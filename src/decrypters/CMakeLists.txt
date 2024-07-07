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

add_subdirectory(clearkey)
if(NOT CORE_SYSTEM_NAME STREQUAL ios AND NOT CORE_SYSTEM_NAME STREQUAL darwin_embedded)
  if(CORE_SYSTEM_NAME STREQUAL android)
    add_subdirectory(widevineandroid)
  else()
    add_subdirectory(widevine)
  endif()
endif()
