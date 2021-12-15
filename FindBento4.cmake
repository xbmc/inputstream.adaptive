#.rst:
# FindBento4
# --------
# Finds the Bento4 library
#
# This will define the following variables::
#
# BENTO4_FOUND - system has Bento4
# BENTO4_INCLUDE_DIRS - the Bento4 include directory
# BENTO4_LIBRARIES - the Bento4 libraries

if(ENABLE_INTERNAL_BENTO4)
  include(ExternalProject)
  file(STRINGS ${CMAKE_SOURCE_DIR}/depends/common/bento4/bento4.txt bentourl REGEX "^bento4[\t ]*.+$")
  string(REGEX REPLACE "^bento4[\t ]*(.+)[\t ]*$" "\\1" url "${bentourl}")

  # allow user to override the download URL with a local tarball
  # needed for offline build envs
  if(BENTO4_URL)
      get_filename_component(BENTO4_URL "${BENTO4_URL}" ABSOLUTE)
  else()
      set(BENTO4_URL ${url})
  endif()
  if(VERBOSE)
      message(STATUS "BENTO4_URL: ${BENTO4_URL}")
  endif()

  set(BENTO4_LIBRARY ${CMAKE_BINARY_DIR}/bento4/install/lib/libap4.a)
  set(BENTO4_INCLUDE_DIR ${CMAKE_BINARY_DIR}/bento4/install/include)

  find_program(PATCH_PROGRAM NAMES patch REQUIRED)
  file(GLOB patches ${CMAKE_SOURCE_DIR}/depends/common/bento4/*.patch)
  list(SORT patches)
  foreach(patch ${patches})
    message(STATUS "bento4: adding patch ${patch}")
    list(APPEND PATCH_COMMAND COMMAND ${PATCH_PROGRAM} -p1 -i ${patch})
  endforeach()

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
  externalproject_add(bento4
                      URL ${BENTO4_URL}
                      DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/download
                      PREFIX ${CMAKE_BINARY_DIR}/bento4
                      PATCH_COMMAND ${PATCH_COMMAND}
                      CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/bento4/install
                                 -DCMAKE_INSTALL_INCLUDEDIR=include
                                 -DCMAKE_INSTALL_LIBDIR=lib
                                 -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
                                 -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                                 -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
                                 -DBUILD_APPS=OFF
                      BUILD_BYPRODUCTS ${BENTO4_LIBRARY})
else()
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_BENTO4 REQUIRED)
  endif()

  find_path(BENTO4_INCLUDE_DIR NAMES bento4/Ap4.h PATHS ${PC_BENTO4_INCLUDEDIR})

  find_library(BENTO4_LIBRARY NAMES ap4 PATHS ${PC_BENTO4_LIBDIR})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Bento4 REQUIRED_VARS BENTO4_LIBRARY BENTO4_INCLUDE_DIR)

if(BENTO4_FOUND)
  set(BENTO4_LIBRARIES ${BENTO4_LIBRARY})
  set(BENTO4_INCLUDE_DIRS ${BENTO4_INCLUDE_DIR})
endif()

mark_as_advanced(BENTO4_INCLUDE_DIR BENTO4_LIBRARY)
