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

execute_process(COMMAND ${CMAKE_COMMAND}
    -G "${CMAKE_GENERATOR}"
    -A "${CMAKE_GENERATOR_PLATFORM}"
    -T "${CMAKE_GENERATOR_TOOLSET}"
    ${CMAKE_SOURCE_DIR}/lib/libbento4
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

execute_process(COMMAND ${CMAKE_COMMAND}
    --build .
    --config ${CMAKE_BUILD_TYPE}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

find_path(BENTO4_INCLUDE_DIR NAMES bento4/Ap4.h PATHS ${CMAKE_BINARY_DIR}/depends/include)
find_library(BENTO4_LIBRARY NAMES ap4 PATHS ${CMAKE_BINARY_DIR}/depends/lib)

set(BENTO4_FOUND TRUE)
set(BENTO4_LIBRARIES ${BENTO4_LIBRARY})
set(BENTO4_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/depends/include)
