cmake_minimum_required(VERSION 3.5)
project(mpegts)

if(TSDEMUX_DEBUG)
  add_definitions(-DTSDEMUX_DEBUG)
endif(TSDEMUX_DEBUG)

add_library(mpegts STATIC
  mpegts/elementaryStream.cpp
  mpegts/bitstream.cpp
  mpegts/ES_AC3.cpp
  mpegts/ES_MPEGAudio.cpp
  mpegts/ES_Subtitle.cpp
  mpegts/ES_AAC.cpp
  mpegts/ES_h264.cpp
  mpegts/ES_MPEGVideo.cpp
  mpegts/ES_Teletext.cpp
  mpegts/ES_hevc.cpp
  mpegts/debug.cpp
  mpegts/tsDemuxer.cpp
)

target_include_directories(mpegts PRIVATE ${PROJECT_SOURCE_DIR}/mpegts PUBLIC ${PROJECT_SOURCE_DIR})

set_target_properties(mpegts PROPERTIES POSITION_INDEPENDENT_CODE True)
