set(SOURCES
  AdaptiveByteStream.cpp
  main.cpp
  oscompat.cpp
  Session.cpp
  Stream.cpp
  TSReader.cpp
  aes_decrypter.cpp
  ADTSReader.cpp
  WebmReader.cpp
)

set(HEADERS
  AdaptiveByteStream.h
  main.h
  oscompat.h
  Session.h
  Stream.h
  TSReader.h
  aes_decrypter.h
  ADTSReader.h
  Iaes_decrypter.h
  WebmReader.h
)

add_dir_sources(SOURCES HEADERS)

add_subdirectory(codechandler)
add_subdirectory(common)
add_subdirectory(decrypters)
add_subdirectory(parser)
add_subdirectory(samplereader)
add_subdirectory(utils)
