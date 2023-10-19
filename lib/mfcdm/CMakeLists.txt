cmake_minimum_required(VERSION 3.10)
project(mfcdm_library)

add_library(mfcdm_library STATIC
  mfcdm/MediaFoundationCdm.h
  mfcdm/MediaFoundationCdm.cpp
  mfcdm/MediaFoundationCdmFactory.cpp
  mfcdm/MediaFoundationCdmSession.cpp
  mfcdm/MediaFoundationSession.cpp
  mfcdm/Log.cpp
)

target_include_directories(mfcdm_library PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(mfcdm_library PRIVATE cdm_library propsys mf mfplat mfplay mfreadwrite mfuuid wmcodecdspuuid)
set_target_properties(mfcdm_library PROPERTIES POSITION_INDEPENDENT_CODE True)
