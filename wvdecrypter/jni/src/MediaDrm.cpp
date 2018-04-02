/*
 *      Copyright (C) 2016 Team Kodi
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "MediaDrm.h"
#include "MediaDrmOnEventListener.h"
#include "UUID.h"
#include "HashMap.h"

#include "jutils-details.hpp"

using namespace jni;

CJNIMediaDrm::CJNIMediaDrm(const CJNIUUID& uuid)
  : CJNIBase("android/media/MediaDrm")
{
  m_object = new_object(GetClassName(), "<init>", "(Ljava/util/UUID;)V",
                        uuid.get_raw());
  m_object.setGlobal();
}

void CJNIMediaDrm::release() const
{
  call_method<void>(m_object,
    "release", "()V");
}

std::vector<char> CJNIMediaDrm::openSession() const
{
  JNIEnv *env = xbmc_jnienv();
  jhbyteArray array = call_method<jhbyteArray>(m_object,
    "openSession", "()[B");

  jsize size = env->GetArrayLength(array.get());

  std::vector<char> result;
  result.resize(size);
  env->GetByteArrayRegion(array.get(), 0, size, (jbyte*)result.data());

  return result;
}

void CJNIMediaDrm::closeSession(const std::vector<char> & sessionId) const
{
  jsize size;
  JNIEnv *env = xbmc_jnienv();

  size = sessionId.size();
  jbyteArray SID = env->NewByteArray(size);
  jbyte *bytedata = (jbyte*)sessionId.data();
  env->SetByteArrayRegion(SID, 0, size, bytedata);

  call_method<void>(m_object,
    "closeSession", "([B)V", SID);

  env->DeleteLocalRef(SID);
}

std::string CJNIMediaDrm::getPropertyString(const std::string &propertyName) const
{
  return jcast<std::string>(call_method<jhstring>(m_object,
    "getPropertyString", "(Ljava/lang/String;)Ljava/lang/String;",
    jcast<jhstring>(propertyName)));
}


void CJNIMediaDrm::setPropertyString(const std::string &propertyName, const std::string &value) const
{
  call_method<void>(m_object,
    "setPropertyString", "(Ljava/lang/String;Ljava/lang/String;)V",
    jcast<jhstring>(propertyName), jcast<jhstring>(value));
}

CJNIMediaDrmKeyRequest CJNIMediaDrm::getKeyRequest(const std::vector<char> &scope, 
  const std::vector<char> &init, const std::string &mimeType, int keyType,
  const std::map<std::string, std::string> &optionalParameters) const
{
  JNIEnv *env = xbmc_jnienv();

  jsize size = scope.size();
  jbyteArray scope_ = env->NewByteArray(size);
  jbyte *bytedata = (jbyte*)scope.data();
  env->SetByteArrayRegion(scope_, 0, size, bytedata);

  size = init.size();
  jbyteArray init_ = env->NewByteArray(size);
  bytedata = (jbyte*)init.data();
  env->SetByteArrayRegion(init_, 0, size, bytedata);

  CJNIHashMap hashMap;
  for (const auto &item : optionalParameters)
    hashMap.put(jcast<jhstring>(item.first), jcast<jhstring>(item.second));

  CJNIMediaDrmKeyRequest result =
    call_method<jhobject>(m_object,
      "getKeyRequest", "([B[BLjava/lang/String;ILjava/util/HashMap;)Landroid/media/MediaDrm$KeyRequest;",
      scope_, init_, jcast<jhstring>(mimeType), keyType, hashMap.get_raw());

  env->DeleteLocalRef(scope_);
  env->DeleteLocalRef(init_);

  return result;
}

std::vector<char> CJNIMediaDrm::provideKeyResponse(const std::vector<char> &scope, const std::vector<char> &response) const
{
  JNIEnv *env = xbmc_jnienv();

  jsize size = scope.size();
  jbyteArray scope_ = env->NewByteArray(size);
  jbyte *bytedata = (jbyte*)scope.data();
  env->SetByteArrayRegion(scope_, 0, size, bytedata);

  size = response.size();
  jbyteArray response_ = env->NewByteArray(size);
  bytedata = (jbyte*)response.data();
  env->SetByteArrayRegion(response_, 0, size, bytedata);

  jhbyteArray array = call_method<jhbyteArray>(m_object,
    "provideKeyResponse", "([B[B)[B", scope_, response_);

  size = env->GetArrayLength(array.get());

  std::vector<char> result;
  result.resize(size);
  env->GetByteArrayRegion(array.get(), 0, size, (jbyte*)result.data());

  env->DeleteLocalRef(scope_);
  env->DeleteLocalRef(response_);

  return result;
}

CJNIMediaDrmProvisionRequest CJNIMediaDrm::getProvisionRequest() const
{
  return call_method<jhobject>(m_object,
      "getProvisionRequest", "()Landroid/media/MediaDrm$ProvisionRequest;");
}

void CJNIMediaDrm::provideProvisionResponse(const std::vector<char> &response) const
{
  JNIEnv *env = xbmc_jnienv();

  jsize size = response.size();
  jbyteArray response_ = env->NewByteArray(size);
  jbyte *bytedata = (jbyte*)response.data();
  env->SetByteArrayRegion(response_, 0, size, bytedata);

  call_method<void>(m_object,
    "provideProvisionResponse", "([B)V", response_);
}

void CJNIMediaDrm::removeKeys(const std::vector<char> &sessionId) const
{
  JNIEnv *env = xbmc_jnienv();

  jsize size = sessionId.size();
  jbyteArray SID = env->NewByteArray(size);
  jbyte *bytedata = (jbyte*)sessionId.data();
  env->SetByteArrayRegion(SID, 0, size, bytedata);

  call_method<void>(m_object,
    "removeKeys", "([B)V", SID);

  env->DeleteLocalRef(SID);
}

void CJNIMediaDrm::setOnEventListener(const CJNIMediaDrmOnEventListener &listener) const
{
  call_method<void>(m_object, "setOnEventListener",
    "(Landroid/media/MediaDrm$OnEventListener;)V",
    listener.get_raw());
}
