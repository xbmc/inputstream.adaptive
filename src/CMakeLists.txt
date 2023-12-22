set(SOURCES
  AdaptiveByteStream.cpp
  main.cpp
  oscompat.cpp
  Session.cpp
  SrvBroker.cpp
  CompKodiProps.cpp
  CompSettings.cpp
  Stream.cpp
  aes_decrypter.cpp
)

set(HEADERS
  AdaptiveByteStream.h
  main.h
  oscompat.h
  Session.h
  SrvBroker.h
  CompKodiProps.h
  CompResources.h
  CompSettings.h
  Stream.h
  aes_decrypter.h
  Iaes_decrypter.h
)

add_dir_sources(SOURCES HEADERS)

add_subdirectory(codechandler)
add_subdirectory(common)
add_subdirectory(decrypters)
add_subdirectory(demuxers)
add_subdirectory(parser)
add_subdirectory(samplereader)
add_subdirectory(utils)
