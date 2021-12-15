cmake_minimum_required(VERSION 3.5)
project(libbento4)

# check if we have patches to apply
file(GLOB patches ${CMAKE_CURRENT_SOURCE_DIR}/*.patch)
list(SORT patches)
foreach(patch ${patches})
  if(NOT PATCH_PROGRAM OR "${PATCH_PROGRAM}" STREQUAL "")
    if(NOT PATCH_EXECUTABLE)
      # find the path to the patch executable
      find_program(PATCH_EXECUTABLE NAMES patch)

      if(NOT PATCH_EXECUTABLE)
        message(FATAL_ERROR "Missing patch command (we looked in ${CMAKE_PREFIX_PATH})")
      endif()
    endif()

    set(PATCH_PROGRAM ${PATCH_EXECUTABLE})

    # On Windows "patch.exe" can only handle CR-LF line-endings.
    # Our patches have LF-only line endings - except when they
    # have been checked out as part of a dependency hosted on Git
    # and core.autocrlf=true.
    if(WIN32)
      file(READ ${patch} patch_content_hex HEX)
      # Force handle LF-only line endings
      if(NOT patch_content_hex MATCHES "0d0a")
        list(APPEND PATCH_PROGRAM --binary)
      endif()
    endif()
  endif()

  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${patch})
  list(APPEND PATCH_COMMAND COMMAND ${PATCH_PROGRAM} -p1 -i ${patch})
endforeach()

include(ExternalProject)
externalproject_add(bento4
                    URL ${CMAKE_CURRENT_SOURCE_DIR}/v1.6.0-639.tar.gz
                    INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/depends
                    PATCH_COMMAND ${PATCH_COMMAND}
                    CMAKE_ARGS -DBUILD_APPS=OFF
                               -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
                               -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                               -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
                               -DBUILD_SHARED_LIBS=OFF
                               -DCMAKE_POSITION_INDEPENDENT_CODE=ON)
