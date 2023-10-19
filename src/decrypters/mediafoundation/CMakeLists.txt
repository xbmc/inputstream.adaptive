set(SOURCES
  MFDecrypter.cpp
  MFCencSingleSampleDecrypter.cpp
)

set(HEADERS
  MFDecrypter.h
  MFCencSingleSampleDecrypter.h
)

add_dir_sources(SOURCES HEADERS)

add_dependency(mfcdm_library lib/mfcdm)
