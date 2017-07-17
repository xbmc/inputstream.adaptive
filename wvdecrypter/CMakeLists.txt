cmake_minimum_required(VERSION 2.6)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    # require at least gcc 4.8
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
        message(FATAL_ERROR "GCC version must be at least 4.8!")
    endif()
endif()

add_definitions(-DUNICODE -D_UNICODE -D_DEBUG)

project(wvdecrypter)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++0x")

if(CORE_SYSTEM_NAME STREQUAL android)
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--no-undefined")
  include_directories (
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libbento4/Core
  )
else()
  include_directories (
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libbento4/Core/
    ${CMAKE_CURRENT_SOURCE_DIR}/cdm
  )
endif()

set(BENTOUSESTCFS 1)

subdirs (
   ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libbento4
)

if(CORE_SYSTEM_NAME STREQUAL android)
  add_library ( ssd_wv SHARED
	wvdecrypter_android.cpp
	jsmn.c
	../src/helpers.cpp
  )
else()
  if(WIN32)
    set(CDMTYPE "win.cc")
    add_definitions("-DWIDEVINECDMFILENAME=\"widevinecdm.dll\"")
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CDMTYPE "mac.mm")
    add_definitions("-DWIDEVINECDMFILENAME=\"libwidevinecdm.dylib\"")
  else()
    set(CDMTYPE "posix.cc")
    add_definitions("-DWIDEVINECDMFILENAME=\"libwidevinecdm.so\"")
  endif()

  add_library ( ssd_wv SHARED
        wvdecrypter.cpp
        jsmn.c
        ../src/helpers.cpp
        cdm/base/native_library.cc
        cdm/base/native_library_${CDMTYPE}
        cdm/media/cdm/cdm_adapter.cc
  )
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  target_link_libraries (ssd_wv
    bento4
    "-framework CoreFoundation"
  )
elseif(CORE_SYSTEM_NAME STREQUAL android)
  target_link_libraries (ssd_wv
    mediandk
    bento4
  )
else()
  target_link_libraries (ssd_wv
    ${CMAKE_DL_LIBS}
    bento4
  )
endif()
