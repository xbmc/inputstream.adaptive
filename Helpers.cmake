# IDEs: Group source files in target in folders (file system hierarchy)
# Source: taken from xbmc\cmake\scripts\common\AddonHelpers.cmake
# Arguments:
#   target The target that shall be grouped by folders.
# Optional Arguments:
#   RELATIVE allows to specify a different reference folder.
function(source_group_by_folder target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "There is no target named '${target}'")
  endif()

  set(SOURCE_GROUP_DELIMITER "/")

  cmake_parse_arguments(arg "" "RELATIVE" "" ${ARGN})
  if(arg_RELATIVE)
    set(relative_dir ${arg_RELATIVE})
  else()
    set(relative_dir ${CMAKE_CURRENT_SOURCE_DIR})
  endif()

  get_property(files TARGET ${target} PROPERTY SOURCES)
  if(files)
    list(SORT files)

    if(CMAKE_GENERATOR STREQUAL Xcode)
      set_target_properties(${target} PROPERTIES SOURCES "${files}")
    endif()
  endif()
  foreach(file ${files})
    if(NOT IS_ABSOLUTE ${file})
      set(file ${CMAKE_CURRENT_SOURCE_DIR}/${file})
    endif()
    file(RELATIVE_PATH relative_file ${relative_dir} ${file})
    get_filename_component(dir "${relative_file}" DIRECTORY)
    if(NOT dir STREQUAL "${last_dir}")
      if(files)
        source_group("${last_dir}" FILES ${files})
      endif()
      set(files "")
    endif()
    set(files ${files} ${file})
    set(last_dir "${dir}")
  endforeach(file)
  if(files)
    source_group("${last_dir}" FILES ${files})
  endif()
endfunction()

# Function to add source files to global properties GlobalSourceList/GlobalHeaderList
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

# Function to add an additional dependency to global properties GlobalDepsNamesList/GlobalDepsFoldersList
function(add_dependency project_name folder)
    set_property(GLOBAL APPEND PROPERTY GlobalDepsNamesList "${project_name}")
    set_property(GLOBAL APPEND PROPERTY GlobalDepsFoldersList "${folder}")
endfunction(add_dependency)

# Function to add an additional shared dependency to global properties GlobalSharedDepsNamesList/GlobalSharedDepsFoldersList
function(add_shared_dependency project_name folder)
    set_property(GLOBAL APPEND PROPERTY GlobalSharedDepsNamesList "${project_name}")
    set_property(GLOBAL APPEND PROPERTY GlobalSharedDepsFoldersList "${folder}")
endfunction(add_shared_dependency)
