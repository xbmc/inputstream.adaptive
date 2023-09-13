cmake_minimum_required(VERSION 3.5)
project(mpegts)

if(TSDEMUX_DEBUG)
  add_definitions(-DTSDEMUX_DEBUG)
endif(TSDEMUX_DEBUG)

add_library(mpegts STATIC
  elementaryStream.cpp
  bitstream.cpp
  ES_AC3.cpp
  ES_MPEGAudio.cpp
  ES_Subtitle.cpp
  ES_AAC.cpp
  ES_h264.cpp
  ES_MPEGVideo.cpp
  ES_Teletext.cpp
  ES_hevc.cpp
  debug.cpp
  tsDemuxer.cpp)

target_include_directories(mpegts PUBLIC ${PROJECT_SOURCE_DIR})

set_target_properties(mpegts PROPERTIES POSITION_INDEPENDENT_CODE True)
