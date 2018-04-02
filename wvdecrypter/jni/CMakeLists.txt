cmake_minimum_required(VERSION 2.8)
project(jni)

# jni Library
file(GLOB JNI_SOURCES
  jutils/*.cpp
  src/*.cpp
)
include_directories(BEFORE ${PROJECT_SOURCE_DIR}/jutils)
add_library(jni STATIC ${JNI_SOURCES})
