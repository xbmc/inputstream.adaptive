cmake_minimum_required(VERSION 3.10)
project(cdm_library)

if(WIN32)
  set(CDMTYPE "win.cc")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(CDMTYPE "mac.mm")
else()
  set(CDMTYPE "posix.cc")
endif()

add_library(cdm_library STATIC
  cdm/base/native_library.cc
  cdm/base/native_library_${CDMTYPE}
  cdm/media/cdm/cdm_adapter.cc
  cdm/media/cdm/cdm_adapter.h
)

target_include_directories(cdm_library PUBLIC ${PROJECT_SOURCE_DIR})

set_target_properties(cdm_library PROPERTIES POSITION_INDEPENDENT_CODE True)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  target_link_libraries(cdm_library "-framework CoreFoundation")
endif()
