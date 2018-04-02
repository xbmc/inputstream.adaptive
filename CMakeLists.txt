project(inputstream.adaptive)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(Kodi REQUIRED)

set(ADP_SOURCES
	src/main.cpp
	src/common/AdaptiveTree.cpp
	src/parser/DASHTree.cpp
	src/parser/HLSTree.cpp
	src/parser/SmoothTree.cpp
	src/parser/TTML.cpp
	src/common/AdaptiveStream.cpp
	src/helpers.cpp
	src/oscompat.cpp
	src/TSReader.cpp
	src/aes_decrypter.cpp
	src/ADTSReader.cpp
  src/md5.cpp
	)

set(ADP_HEADERS
	src/helpers.h
	src/main.h
	src/oscompat.h
	src/SSD_dll.h
	src/common/AdaptiveStream.h
	src/common/AdaptiveTree.h
	src/parser/DASHTree.h
	src/parser/HLSTree.h
	src/parser/SmoothTree.h
	src/parser/TTML.h
	src/TSReader.h
	src/log.h
	src/aes_decrypter.h
	src/ADTSReader.h
  src/md5.h
	)

if(WIN32)
  find_package(p8-platform REQUIRED)
  list(APPEND DEPLIBS ${p8-platform_LIBRARIES})
  include_directories(${p8-platform_INCLUDE_DIRS})
endif()

include_directories(${INCLUDES}
                    ${KODI_INCLUDE_DIR}
                    lib/libbento4/Core
)

include(FindEXPAT)
find_package(EXPAT REQUIRED)
include_directories(${EXPAT_INCLUDE_DIRS})
list(APPEND DEPLIBS ${EXPAT_LIBRARIES})

if(WIN32)
  add_definitions(-DXML_STATIC -D_CRT_NONSTDC_NO_DEPRECATE)
  list(APPEND DEPLIBS ws2_32)
else()
  add_definitions(-D__STDC_FORMAT_MACROS)
endif()

add_definitions(-DUNICODE -D_UNICODE)

add_subdirectory(lib/mpegts)

if(NOT CORE_SYSTEM_NAME STREQUAL ios)
  add_subdirectory(wvdecrypter)
  set(ADP_ADDITIONAL_BINARY $<TARGET_FILE:ssd_wv>)
else()
  set(BENTOUSESTCFS 1)
  add_subdirectory(lib/libbento4)
endif()

if(CORE_SYSTEM_NAME STREQUAL android)
  set(DECRYPTERPATH "special://xbmcbinaddons")
else()
  set(DECRYPTERPATH "special://home/cdm")
endif()

list(APPEND DEPLIBS bento4)
list(APPEND DEPLIBS mpegts)

build_addon(inputstream.adaptive ADP DEPLIBS)

include(CPack)
