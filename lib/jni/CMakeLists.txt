cmake_minimum_required(VERSION 3.5)
project(jni)

add_library(jni STATIC
  jni/jutils/jutils-details.hpp
  jni/jutils/jutils.cpp
  jni/jutils/jutils.hpp
  jni/src/ClassLoader.cpp
  jni/src/ClassLoader.h
  jni/src/ClassLoader.cpp
  jni/src/HashMap.cpp
  jni/src/HashMap.h
  jni/src/JNIBase.cpp
  jni/src/JNIBase.h
  jni/src/Log.h
  jni/src/MediaDrm.cpp
  jni/src/MediaDrm.h
  jni/src/MediaDrmKeyRequest.cpp
  jni/src/MediaDrmKeyRequest.h
  jni/src/MediaDrmOnEventListener.cpp
  jni/src/MediaDrmOnEventListener.h
  jni/src/MediaDrmProvisionRequest.cpp
  jni/src/MediaDrmProvisionRequest.h
  jni/src/UUID.cpp
  jni/src/UUID.h
)

target_include_directories(jni PUBLIC ${PROJECT_SOURCE_DIR} PRIVATE ${PROJECT_SOURCE_DIR}/jni/jutils)

source_group_by_folder(jni)
