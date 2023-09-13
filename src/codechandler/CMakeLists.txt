set(SOURCES
  CodecHandler.cpp
  AudioCodecHandler.cpp
  AV1CodecHandler.cpp
  AVCCodecHandler.cpp
  HEVCCodecHandler.cpp
  TTMLCodecHandler.cpp
  VP9CodecHandler.cpp
  WebVTTCodecHandler.cpp
  ttml/TTML.cpp
)

set(HEADERS
  CodecHandler.h
  AudioCodecHandler.h
  AV1CodecHandler.h
  AVCCodecHandler.h
  HEVCCodecHandler.h
  TTMLCodecHandler.h
  VP9CodecHandler.h
  WebVTTCodecHandler.h
  ttml/TTML.h
)

add_dir_sources(SOURCES HEADERS)
