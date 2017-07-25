cmake_minimum_required(VERSION 2.6)
project(mpegts)

include_directories ("${CMAKE_CURRENT_SOURCE_DIR}/.")

if(TSDEMUX_DEBUG)
  add_definitions(-DTSDEMUX_DEBUG)
endif(TSDEMUX_DEBUG)

add_library(mpegts STATIC
  src/elementaryStream.cpp
  src/bitstream.cpp
  src/ES_AC3.cpp
  src/ES_MPEGAudio.cpp
  src/ES_Subtitle.cpp
  src/ES_AAC.cpp
  src/ES_h264.cpp
  src/ES_MPEGVideo.cpp
  src/ES_Teletext.cpp
  src/ES_hevc.cpp
  src/debug.cpp
  src/tsDemuxer.cpp)

set_target_properties(mpegts PROPERTIES POSITION_INDEPENDENT_CODE True)
