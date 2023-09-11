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
set(CMAKE_FIND_FRAMEWORK LAST)
# Sets the default path where the CDM library is contained (affects the default value of the add-on settings)
set(DECRYPTERPATH "special://home/cdm")

find_package(Kodi REQUIRED)
find_package(pugixml REQUIRED)
find_package(Bento4 REQUIRED)

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
endif()

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
