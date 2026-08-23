cmake_minimum_required(VERSION 3.18...4.0)

set(CMAKE_CXX_STANDARD 20)

add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/MP>)

project(inputstream.adaptive)

include(Helpers.cmake)

option(BUILD_TESTING "Build the testing tree." ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})
set(CMAKE_FIND_FRAMEWORK LAST)
# Sets the default path where the CDM library is contained (affects the default value of the add-on settings)
set(DECRYPTERPATH "special://home/cdm")

find_package(Kodi REQUIRED)
find_package(pugixml REQUIRED)
find_package(Bento4 REQUIRED)
find_package(NlohmannJSON REQUIRED)

# JNI android wrapper library
if(CORE_SYSTEM_NAME STREQUAL android)
  find_package(LibAndroidJNI REQUIRED)
endif()

if(CMAKE_CXX_COMPILER MATCHES "arm-webos")
  add_compile_definitions(TARGET_WEBOS=1)
endif()

if(WIN32)
  if(MSVC)
    # Allow build for WindowsStore with Visual Studio
    add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
  endif()
  # Allow unicode build for windows api
  add_definitions(-DUNICODE -D_UNICODE)
  # dlfcn implementation for windows, allow to use e.g. dlopen as POSIX-compatible way
  find_package(dlfcn-win32 REQUIRED)
  list(APPEND DEPLIBS ${dlfcn-win32_LIBRARIES})
  include_directories(${dlfcn-win32_INCLUDE_DIRS})
else()
  # Required on some old linux platforms to use macro like PRIu64
  add_definitions(-D__STDC_FORMAT_MACROS)
  # Force symbol visibility hidden by default for operative systems different than Windows,
  # by default on Windows is hidden, on linux is the opposite, and lead to have singleton static objects
  # stored in memory in a persistent way between all ISAdaptive instances (kodi core dlopen/dlclose)
  add_compile_options(-fvisibility=hidden)
endif()

# Sources to build
# (use add_dir_sources function to add source/header files from the CMakeLists files of subdirectories)
add_subdirectory(src)
# Appends sources declared by add_dir_sources to main ADP_SOURCES/ADP_HEADERS lists,
# build_addon function requires those variables
get_property(SOURCES_LIST GLOBAL PROPERTY GlobalSourceList)
list(APPEND ADP_SOURCES ${SOURCES_LIST})
get_property(HEADERS_LIST GLOBAL PROPERTY GlobalHeaderList)
list(APPEND ADP_HEADERS ${HEADERS_LIST})

include_directories(${INCLUDES}
                    ${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways (becomes done in future)
                    ${NLOHMANNJSON_INCLUDE_DIRS}
                    $<$<STREQUAL:${CORE_SYSTEM_NAME},android>:${LIBANDROIDJNI_INCLUDE_DIRS}>
                    src/
)

add_subdirectory(lib/mpegts)
add_subdirectory(lib/webm_parser)

if(ENABLE_INTERNAL_BENTO4)
  include_directories(${BENTO4_INCLUDE_DIRS})
  add_dependencies(mpegts bento4)
  add_dependencies(webm_parser bento4)
endif()

list(APPEND DEPLIBS ${PUGIXML_LIBRARIES}
                    ${BENTO4_LIBRARIES}
                    mpegts
                    webm_parser
)

if(CORE_SYSTEM_NAME STREQUAL android)
  list(APPEND DEPLIBS ${LIBANDROIDJNI_LIBRARIES})
  list(APPEND DEPLIBS log) # android log library, used by libandroidjni to print logs
endif()

# Add additional dependencies
get_property(DEPS_FOLDERS_LIST GLOBAL PROPERTY GlobalDepsFoldersList)
foreach(DEP_FOLDER ${DEPS_FOLDERS_LIST})
  add_subdirectory(${DEP_FOLDER})
endforeach()
get_property(DEPS_NAMES_LIST GLOBAL PROPERTY GlobalDepsNamesList)
list(APPEND DEPLIBS ${DEPS_NAMES_LIST})

build_addon(inputstream.adaptive ADP DEPLIBS)

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

include(CPack)
