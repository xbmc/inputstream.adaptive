project(expat)

cmake_minimum_required(VERSION 2.8)

include(ExternalProject)

if(NOT WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/config.sub" AND EXISTS "${CMAKE_SOURCE_DIR}/config.guess")
  message(STATUS "Copying autoconf files")
  file(APPEND ${CMAKE_SOURCE_DIR}/acpatch.cmake
               "file(COPY ${CMAKE_SOURCE_DIR}/config.sub ${CMAKE_SOURCE_DIR}/config.guess
               DESTINATION ${CMAKE_SOURCE_DIR}/conftools/)\n")
  set(CP_AUTOCONF ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/acpatch.cmake)
endif()

externalproject_add(expat
                    SOURCE_DIR ${CMAKE_SOURCE_DIR}
                    PATCH_COMMAND ${CP_AUTOCONF}
                    INSTALL_COMMAND ""
                    BUILD_IN_SOURCE 1
                    CONFIGURE_COMMAND ./configure --prefix=${OUTPUT_DIR} --disable-shared
                                      "CFLAGS=-fPIC")

install(CODE "execute_process(COMMAND make install WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})")
