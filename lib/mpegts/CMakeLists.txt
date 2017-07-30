cmake_minimum_required(VERSION 2.6)
project(mpegts)

include_directories ("${CMAKE_CURRENT_SOURCE_DIR}/.")

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

set_target_properties(mpegts PROPERTIES POSITION_INDEPENDENT_CODE True)
