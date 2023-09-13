cmake_minimum_required(VERSION 3.5)
project(mpegts)

if(TSDEMUX_DEBUG)
  add_definitions(-DTSDEMUX_DEBUG)
endif(TSDEMUX_DEBUG)

add_library(mpegts STATIC
  mpegts/debug.cpp
  mpegts/debug.h
  mpegts/elementaryStream.cpp
  mpegts/elementaryStream.h
  mpegts/ES_AAC.cpp
  mpegts/ES_AAC.h
  mpegts/ES_AC3.cpp
  mpegts/ES_AC3.h
  mpegts/ES_h264.cpp
  mpegts/ES_h264.h
  mpegts/ES_hevc.cpp
  mpegts/ES_hevc.h
  mpegts/ES_MPEGAudio.cpp
  mpegts/ES_MPEGAudio.h
  mpegts/ES_MPEGVideo.cpp
  mpegts/ES_MPEGVideo.h
  mpegts/ES_Subtitle.cpp
  mpegts/ES_Subtitle.h
  mpegts/ES_Teletext.cpp
  mpegts/ES_Teletext.h
  mpegts/mutex.h
  mpegts/tsDemuxer.cpp
  mpegts/tsDemuxer.h
  mpegts/tsPacket.h
  mpegts/tsTable.h
  mpegts/bitstream.cpp
  mpegts/bitstream.h
)

target_include_directories(mpegts PRIVATE ${PROJECT_SOURCE_DIR}/mpegts PUBLIC ${PROJECT_SOURCE_DIR})

set_target_properties(mpegts PROPERTIES POSITION_INDEPENDENT_CODE True)

source_group_by_folder(mpegts)
