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
  JNIEnv *env = xbmc_jnienv();
  call_method<void>(m_object,
    "closeSession", "([B)V", jcast<jhbyteArray, std::vector<char> >(sessionId));
}

std::string CJNIMediaDrm::getPropertyString(const std::string &propertyName) const
{
  return jcast<std::string>(call_method<jhstring>(m_object,
    "getPropertyString", "(Ljava/lang/String;)Ljava/lang/String;",
    jcast<jhstring>(propertyName)));
}

std::vector<char> CJNIMediaDrm::getPropertyByteArray(const std::string &propertyName) const
{
  JNIEnv *env = xbmc_jnienv();
  jhbyteArray array = call_method<jhbyteArray>(m_object,
    "getPropertyByteArray", "(Ljava/lang/String;)[B",
      jcast<jhstring>(propertyName));

  jsize size = env->GetArrayLength(array.get());

  std::vector<char> result;
  result.resize(size);
  env->GetByteArrayRegion(array.get(), 0, size, (jbyte*)result.data());

  return result;
}

void CJNIMediaDrm::setPropertyString(const std::string &propertyName, const std::string &value) const
{
  call_method<void>(m_object,
    "setPropertyString", "(Ljava/lang/String;Ljava/lang/String;)V",
    jcast<jhstring>(propertyName), jcast<jhstring>(value));
}

void CJNIMediaDrm::setPropertyByteArray(const std::string &propertyName, const std::vector<char> &value) const
{
  JNIEnv *env = xbmc_jnienv();
  call_method<void>(m_object,
    "setPropertyByteArray", "(Ljava/lang/String;[B)V",
    jcast<jhstring>(propertyName), jcast<jhbyteArray, std::vector<char> >(value));
}

CJNIMediaDrmKeyRequest CJNIMediaDrm::getKeyRequest(const std::vector<char> &scope, 
  const std::vector<char> &init, const std::string &mimeType, int keyType,
  const std::map<std::string, std::string> &optionalParameters) const
{
  JNIEnv *env = xbmc_jnienv();

  CJNIHashMap hashMap;
  for (const auto &item : optionalParameters)
    hashMap.put(jcast<jhstring>(item.first), jcast<jhstring>(item.second));

  CJNIMediaDrmKeyRequest result =
    call_method<jhobject>(m_object,
      "getKeyRequest", "([B[BLjava/lang/String;ILjava/util/HashMap;)Landroid/media/MediaDrm$KeyRequest;",
      jcast<jhbyteArray, std::vector<char> >(scope), jcast<jhbyteArray, std::vector<char> >(init),
      jcast<jhstring>(mimeType), keyType, hashMap.get_raw());

  return result;
}

std::vector<char> CJNIMediaDrm::provideKeyResponse(const std::vector<char> &scope, const std::vector<char> &response) const
{
  JNIEnv *env = xbmc_jnienv();
  jhbyteArray array = call_method<jhbyteArray>(m_object,
    "provideKeyResponse", "([B[B)[B", jcast<jhbyteArray, std::vector<char> >(scope),
    jcast<jhbyteArray, std::vector<char> >(response));

  jsize size = env->GetArrayLength(array.get());

  std::vector<char> result;
  result.resize(size);
  env->GetByteArrayRegion(array.get(), 0, size, (jbyte*)result.data());

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
  call_method<void>(m_object,
    "provideProvisionResponse", "([B)V", jcast<jhbyteArray, std::vector<char> >(response));
}

void CJNIMediaDrm::removeKeys(const std::vector<char> &sessionId) const
{
  JNIEnv *env = xbmc_jnienv();
  call_method<void>(m_object,
    "removeKeys", "([B)V", jcast<jhbyteArray, std::vector<char> >(sessionId));
}

void CJNIMediaDrm::setOnEventListener(const CJNIMediaDrmOnEventListener &listener) const
{
  call_method<void>(m_object, "setOnEventListener",
    "(Landroid/media/MediaDrm$OnEventListener;)V",
    listener.get_raw());
}
