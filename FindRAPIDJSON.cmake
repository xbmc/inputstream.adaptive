# Try find RapidJSON
#
# The following variables are set when RapidJSON is found:
#  RAPIDJSON_FOUND      = Set to true, if all components of RapidJSON have been found.
#  RAPIDJSON_INCLUDES   = Include path for the header files of RapidJSON
#  RAPIDJSON_LIBRARIES  = Link these to use RapidJSON

if (NOT RAPIDJSON_FOUND)
  if (NOT RAPIDJSON_ROOT_DIR)
    set (RAPIDJSON_ROOT_DIR ${CMAKE_INSTALL_PREFIX})
  endif (NOT RAPIDJSON_ROOT_DIR)

  find_path (RAPIDJSON_INCLUDES
    NAMES rapidjson/rapidjson.h rapidjson/reader.h rapidjson/writer.h
    HINTS ${RAPIDJSON_ROOT_DIR} ${CMAKE_INSTALL_PREFIX}
    PATH_SUFFIXES include
  )

  find_package_handle_standard_args (RAPIDJSON DEFAULT_MSG RAPIDJSON_INCLUDES)

  if (RAPIDJSON_FOUND)
    get_filename_component (RAPIDJSON_ROOT_DIR ${RAPIDJSON_INCLUDES} PATH)

    if (NOT RAPIDJSON_FIND_QUIETLY)
      message (STATUS "Found components for RapidJSON")
      message (STATUS "RAPIDJSON_ROOT_DIR  = ${RAPIDJSON_ROOT_DIR}")
      message (STATUS "RAPIDJSON_INCLUDES  = ${RAPIDJSON_INCLUDES}")
    endif (NOT RAPIDJSON_FIND_QUIETLY)
  else (RAPIDJSON_FOUND)
    if (RAPIDJSON_FIND_REQUIRED)
      message (FATAL_ERROR "Could not find RapidJSON!")
    endif (RAPIDJSON_FIND_REQUIRED)
  endif (RAPIDJSON_FOUND)

  mark_as_advanced(RAPIDJSON_ROOT_DIR RAPIDJSON_INCLUDES)

endif (NOT RAPIDJSON_FOUND)
