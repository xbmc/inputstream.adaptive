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

if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_BENTO4 REQUIRED)
endif()

find_path(BENTO4_INCLUDE_DIR NAMES bento4/Ap4.h PATHS ${PC_BENTO4_INCLUDEDIR})

find_library(BENTO4_LIBRARY NAMES ap4 PATHS ${PC_BENTO4_LIBDIR})
                                   
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Bento4 REQUIRED_VARS BENTO4_LIBRARY BENTO4_INCLUDE_DIR)

if(BENTO4_FOUND)
  set(BENTO4_LIBRARIES ${BENTO4_LIBRARY})
  set(BENTO4_INCLUDE_DIRS ${BENTO4_INCLUDE_DIR})
endif()

mark_as_advanced(BENTO4_INCLUDE_DIR BENTO4_LIBRARY)
