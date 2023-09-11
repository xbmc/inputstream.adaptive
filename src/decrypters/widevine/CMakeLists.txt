set(SOURCES
  WVCencSingleSampleDecrypter.cpp
  WVCdmAdapter.cpp
  CdmFixedBuffer.cpp
  WVDecrypter.cpp
  jsmn.c
  CdmTypeConversion.cpp
)

set(HEADERS
  WVCencSingleSampleDecrypter.h
  WVCdmAdapter.h
  CdmFixedBuffer.h
  WVDecrypter.h
  jsmn.h
  CdmTypeConversion.h
)

add_dir_sources(SOURCES HEADERS)

# Native CDM library
add_dependency(cdm_library lib/cdm)
