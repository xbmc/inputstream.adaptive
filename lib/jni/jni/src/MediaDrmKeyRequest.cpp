/*
 *  Copyright (C) 2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaDrmKeyRequest.h"
#include "jutils-details.hpp"

using namespace jni;

CJNIMediaDrmKeyRequest::CJNIMediaDrmKeyRequest()
  : CJNIBase("android/media/MediaDrm$KeyRequest")
{
  m_object = new_object(GetClassName(), "<init>", "()V");
  m_object.setGlobal();
}

std::vector<uint8_t> CJNIMediaDrmKeyRequest::getData() const
{
  JNIEnv *env = xbmc_jnienv();
  jhbyteArray array = call_method<jhbyteArray>(m_object,
    "getData", "()[B");

  jsize size = env->GetArrayLength(array.get());

  std::vector<uint8_t> result;
  result.resize(size);
  env->GetByteArrayRegion(array.get(), 0, size, (jbyte*)result.data());

  return result;
}

int CJNIMediaDrmKeyRequest::getRequestType() const
{
  return call_method<jint>(m_object, "getRequestType", "()I");
}
