cmake_minimum_required(VERSION 3.10)
project(inputstream.adaptive)
option(BUILD_TESTING "Build the testing tree." ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)

set(ADP_SOURCES
	src/main.cpp
	src/codechandler/AVCCodecHandler.cpp
	src/codechandler/CodecHandler.cpp
	src/codechandler/HEVCCodecHandler.cpp
	src/codechandler/MPEGCodecHandler.cpp
	src/codechandler/TTMLCodecHandler.cpp
	src/codechandler/VP9CodecHandler.cpp
	src/codechandler/WebVTTCodecHandler.cpp
	src/codechandler/ttml/TTML.cpp
	src/common/AdaptiveStream.cpp
	src/common/AdaptiveTree.cpp
	src/common/RepresentationChooser.cpp
	src/parser/DASHTree.cpp
	src/parser/HLSTree.cpp
	src/parser/SmoothTree.cpp
	src/parser/PRProtectionParser.cpp
	src/utils/Base64Utils.cpp
	src/utils/StringUtils.cpp
	src/utils/Utils.cpp
	src/oscompat.cpp
	src/TSReader.cpp
	src/aes_decrypter.cpp
	src/ADTSReader.cpp
	src/md5.cpp
	src/WebmReader.cpp
	)

set(ADP_HEADERS
	src/main.h
	src/oscompat.h
	src/SSD_dll.h
	src/codechandler/AVCCodecHandler.h
	src/codechandler/CodecHandler.h
	src/codechandler/HEVCCodecHandler.h
	src/codechandler/MPEGCodecHandler.h
	src/codechandler/TTMLCodecHandler.h
	src/codechandler/VP9CodecHandler.h
	src/codechandler/WebVTTCodecHandler.h
	src/codechandler/ttml/TTML.h
	src/common/AdaptiveStream.h
	src/common/AdaptiveTree.h
	src/common/RepresentationChooser.h
	src/parser/DASHTree.h
	src/parser/HLSTree.h
	src/parser/SmoothTree.h
	src/parser/PRProtectionParser.h
	src/utils/Base64Utils.h
	src/utils/log.h
	src/utils/StringUtils.h
	src/utils/Utils.h
	src/TSReader.h
	src/aes_decrypter.h
	src/ADTSReader.h
	src/Iaes_decrypter.h
	src/md5.h
	src/WebmReader.h
	)

if(WIN32)
  find_package(dlfcn-win32 REQUIRED)
  list(APPEND DEPLIBS ${dlfcn-win32_LIBRARIES})
  include_directories(${dlfcn-win32_INCLUDE_DIRS})
endif()

include_directories(${INCLUDES}
                    ${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways (becomes done in future)
                    lib/webm_parser/include
)

set(CMAKE_FIND_FRAMEWORK LAST)

find_package(EXPAT REQUIRED)
find_package(Bento4 REQUIRED)

if(WIN32)
  add_definitions(-DXML_STATIC -D_CRT_NONSTDC_NO_DEPRECATE)
  list(APPEND DEPLIBS ws2_32)
else()
  add_definitions(-D__STDC_FORMAT_MACROS)
endif()

add_definitions(-DUNICODE -D_UNICODE)

include_directories(${EXPAT_INCLUDE_DIRS})
list(APPEND DEPLIBS ${EXPAT_LIBRARIES})

if(CORE_SYSTEM_NAME STREQUAL ios OR CORE_SYSTEM_NAME STREQUAL darwin_embedded)
  set(BENTOUSESTCFS 1)
  include_directories(${BENTO4_INCLUDE_DIRS})
else()
  add_subdirectory(wvdecrypter)
  set(ADP_ADDITIONAL_BINARY $<TARGET_FILE:ssd_wv>)
endif()

add_subdirectory(lib/mpegts)
add_subdirectory(lib/webm_parser)

if(ENABLE_INTERNAL_BENTO4)
  include_directories(${BENTO4_INCLUDE_DIRS})
  add_dependencies(ssd_wv bento4)
  add_dependencies(mpegts bento4)
  add_dependencies(webm_parser bento4)
endif()

set(DECRYPTERPATH "special://home/cdm")

list(APPEND DEPLIBS ${BENTO4_LIBRARIES})
list(APPEND DEPLIBS mpegts)
list(APPEND DEPLIBS webm_parser)

build_addon(inputstream.adaptive ADP DEPLIBS)

include(CPack)

if(NOT CMAKE_CROSSCOMPILING AND BUILD_TESTING)
  list(APPEND CMAKE_CTEST_ARGUMENTS "--output-on-failure")
  enable_testing()
  include(FindGtest)
  find_package(Gtest REQUIRED)
  include_directories(${GTEST_INCLUDE_DIRS})
  add_subdirectory(src/test)
  if(ENABLE_INTERNAL_BENTO4)
    add_dependencies(Test${CMAKE_PROJECT_NAME} bento4)
  endif()
endif()
