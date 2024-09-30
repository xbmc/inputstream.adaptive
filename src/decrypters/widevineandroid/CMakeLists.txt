set(SOURCES
  WVCencSingleSampleDecrypter.cpp
  WVCdmAdapter.cpp
  WVDecrypter.cpp
)

set(HEADERS
  WVCencSingleSampleDecrypter.h
  WVCdmAdapter.h
  WVDecrypter.h
)

add_dir_sources(SOURCES HEADERS)

# JNI android wrapper library
add_dependency(jni lib/jni)
