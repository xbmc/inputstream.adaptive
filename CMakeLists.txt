cmake_minimum_required(VERSION 3.10)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    # require at least gcc 4.8
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
        message(FATAL_ERROR "GCC version must be at least 4.8!")
    endif()
endif()

project(inputstream.adaptive)
option(BUILD_TESTING "Build the testing tree." ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)

# Function to add source files to the main file list
function(add_dir_sources source_files header_files)
    foreach(_source ${${source_files}})
        if (IS_ABSOLUTE "${_source}")
            set(_source_abs "${_source}")
        else()
            get_filename_component(_source_abs "${_source}" ABSOLUTE)
        endif()
        set_property(GLOBAL APPEND PROPERTY GlobalSourceList "${_source_abs}")
    endforeach()
    foreach(_header ${${header_files}})
        if (IS_ABSOLUTE "${_header}")
            set(_header_abs "${_header}")
        else()
            get_filename_component(_header_abs "${_header}" ABSOLUTE)
        endif()
        set_property(GLOBAL APPEND PROPERTY GlobalHeaderList "${_header_abs}")
    endforeach()
endfunction(add_dir_sources)

# Function to add an additional dependency
function(add_dependency project_name folder)
    set_property(GLOBAL APPEND PROPERTY GlobalDepsNamesList "${project_name}")
    set_property(GLOBAL APPEND PROPERTY GlobalDepsFoldersList "${folder}")
endfunction(add_dependency)

set(ADP_SOURCES
	src/AdaptiveByteStream.cpp
	src/main.cpp
	src/codechandler/CodecHandler.cpp
	src/codechandler/AudioCodecHandler.cpp
	src/codechandler/AV1CodecHandler.cpp
	src/codechandler/AVCCodecHandler.cpp
	src/codechandler/HEVCCodecHandler.cpp
	src/codechandler/TTMLCodecHandler.cpp
	src/codechandler/VP9CodecHandler.cpp
	src/codechandler/WebVTTCodecHandler.cpp
	src/codechandler/ttml/TTML.cpp
	src/common/AdaptationSet.cpp
	src/common/AdaptiveCencSampleDecrypter.cpp
	src/common/AdaptiveStream.cpp
	src/common/AdaptiveTree.cpp
	src/common/AdaptiveTreeFactory.cpp
	src/common/AdaptiveUtils.cpp
	src/common/Chooser.cpp
	src/common/ChooserAskQuality.cpp
	src/common/ChooserDefault.cpp
	src/common/ChooserFixedRes.cpp
	src/common/ChooserManualOSD.cpp
	src/common/ChooserTest.cpp
	src/common/CommonAttribs.cpp
	src/common/CommonSegAttribs.cpp
	src/common/Period.cpp
	src/common/Representation.cpp
	src/common/ReprSelector.cpp
	src/common/Segment.cpp
	src/common/SegmentBase.cpp
	src/common/SegmentList.cpp
	src/common/SegTemplate.cpp
	src/parser/CodecParser.cpp
	src/parser/DASHTree.cpp
	src/parser/HLSTree.cpp
	src/parser/SmoothTree.cpp
	src/parser/PRProtectionParser.cpp
	src/samplereader/ADTSSampleReader.cpp
	src/samplereader/FragmentedSampleReader.cpp
	src/samplereader/SubtitleSampleReader.cpp
	src/samplereader/TSSampleReader.cpp
	src/samplereader/WebmSampleReader.cpp
	src/utils/Base64Utils.cpp
	src/utils/CharArrayParser.cpp
	src/utils/CurlUtils.cpp
	src/utils/DigestMD5Utils.cpp
	src/utils/FileUtils.cpp
	src/utils/MemUtils.cpp
	src/utils/PropertiesUtils.cpp
	src/utils/StringUtils.cpp
	src/utils/SettingsUtils.cpp
	src/utils/UrlUtils.cpp
	src/utils/Utils.cpp
	src/utils/XMLUtils.cpp
	src/oscompat.cpp
	src/Session.cpp
	src/Stream.cpp
	src/TSReader.cpp
	src/aes_decrypter.cpp
	src/ADTSReader.cpp
	src/WebmReader.cpp
	)

set(ADP_HEADERS
	src/AdaptiveByteStream.h
	src/main.h
	src/oscompat.h
	src/codechandler/CodecHandler.h
	src/codechandler/AudioCodecHandler.h
	src/codechandler/AV1CodecHandler.h
	src/codechandler/AVCCodecHandler.h
	src/codechandler/HEVCCodecHandler.h
	src/codechandler/TTMLCodecHandler.h
	src/codechandler/VP9CodecHandler.h
	src/codechandler/WebVTTCodecHandler.h
	src/codechandler/ttml/TTML.h
	src/common/AdaptationSet.h
	src/common/AdaptiveCencSampleDecrypter.h
	src/common/AdaptiveDecrypter.h
	src/common/AdaptiveStream.h
	src/common/AdaptiveTree.h
	src/common/AdaptiveTreeFactory.h
	src/common/AdaptiveUtils.h
	src/common/Chooser.h
	src/common/ChooserAskQuality.h
	src/common/ChooserDefault.h
	src/common/ChooserFixedRes.h
	src/common/ChooserManualOSD.h
	src/common/ChooserTest.h
	src/common/CommonAttribs.h
	src/common/CommonSegAttribs.h
	src/common/Period.h
	src/common/Representation.h
	src/common/ReprSelector.h
	src/common/Segment.h
	src/common/SegmentBase.h
	src/common/SegmentList.h
	src/common/SegTemplate.h
	src/parser/CodecParser.h
	src/parser/DASHTree.h
	src/parser/HLSTree.h
	src/parser/SmoothTree.h
	src/parser/PRProtectionParser.h
	src/samplereader/ADTSSampleReader.h
	src/samplereader/FragmentedSampleReader.h
	src/samplereader/SampleReader.h
	src/samplereader/SubtitleSampleReader.h
	src/samplereader/TSSampleReader.h
	src/samplereader/WebmSampleReader.h
	src/utils/Base64Utils.h
	src/utils/CharArrayParser.h
	src/utils/CryptoUtils.h
	src/utils/CurlUtils.h
	src/utils/DigestMD5Utils.h
	src/utils/FileUtils.h
	src/utils/log.h
	src/utils/MemUtils.h
	src/utils/PropertiesUtils.h
	src/utils/SettingsUtils.h
	src/utils/StringUtils.h
	src/utils/UrlUtils.h
	src/utils/Utils.h
	src/utils/XMLUtils.h
	src/Session.h
	src/Stream.h
	src/TSReader.h
	src/aes_decrypter.h
	src/ADTSReader.h
	src/Iaes_decrypter.h
	src/WebmReader.h
	)

add_subdirectory(src/decrypters)

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

find_package(Bento4 REQUIRED)
find_package(pugixml REQUIRED)

if(WIN32)
  add_definitions(-DXML_STATIC -D_CRT_NONSTDC_NO_DEPRECATE)
  list(APPEND DEPLIBS ws2_32)
else()
  add_definitions(-D__STDC_FORMAT_MACROS)
endif()

add_definitions(-DUNICODE -D_UNICODE)

include_directories(${PUGIXML_INCLUDE_DIRS})

add_subdirectory(lib/mpegts)
add_subdirectory(lib/webm_parser)

if(ENABLE_INTERNAL_BENTO4)
  include_directories(${BENTO4_INCLUDE_DIRS})
  add_dependencies(mpegts bento4)
  add_dependencies(webm_parser bento4)
endif()

set(DECRYPTERPATH "special://home/cdm")

list(APPEND DEPLIBS ${BENTO4_LIBRARIES})
list(APPEND DEPLIBS mpegts)
list(APPEND DEPLIBS webm_parser)
list(APPEND DEPLIBS ${PUGIXML_LIBRARIES})

# Add sources from CMakeLists on subfolders
get_property(SOURCES_LIST GLOBAL PROPERTY GlobalSourceList)
list(APPEND ADP_SOURCES ${SOURCES_LIST})
get_property(HEADERS_LIST GLOBAL PROPERTY GlobalHeaderList)
list(APPEND ADP_HEADERS ${HEADERS_LIST})

# Add additional dependencies
get_property(DEPS_FOLDERS_LIST GLOBAL PROPERTY GlobalDepsFoldersList)
foreach(DEP_FOLDER ${DEPS_FOLDERS_LIST})
  add_subdirectory(${DEP_FOLDER})
endforeach()
get_property(DEPS_NAMES_LIST GLOBAL PROPERTY GlobalDepsNamesList)
list(APPEND DEPLIBS ${DEPS_NAMES_LIST})

build_addon(inputstream.adaptive ADP DEPLIBS)

include(CPack)

if(NOT CMAKE_CROSSCOMPILING AND BUILD_TESTING)
  list(APPEND CMAKE_CTEST_ARGUMENTS "-V")
  enable_testing()
  include(FindGtest)
  find_package(Gtest REQUIRED)
  include_directories(${GTEST_INCLUDE_DIRS})
  add_subdirectory(src/test)
  if(ENABLE_INTERNAL_BENTO4)
    add_dependencies(${CMAKE_PROJECT_NAME}_test bento4)
  endif()
endif()
