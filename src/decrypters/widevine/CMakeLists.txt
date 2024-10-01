set(SOURCES
  WVCencSingleSampleDecrypter.cpp
  WVCdmAdapter.cpp
  CdmFixedBuffer.cpp
  WVDecrypter.cpp
  CdmTypeConversion.cpp
)

set(HEADERS
  WVCencSingleSampleDecrypter.h
  WVCdmAdapter.h
  CdmFixedBuffer.h
  WVDecrypter.h
  CdmTypeConversion.h
)

add_dir_sources(SOURCES HEADERS)

# Native CDM library
add_dependency(cdm_library lib/cdm)

# Patch to load CDM library on aarch64 platforms
if(${CMAKE_SYSTEM_NAME} STREQUAL Linux)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^aarch64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "^arm64")
  add_shared_dependency(cdm_aarch64_loader lib/cdm_aarch64)
endif()
endif()
