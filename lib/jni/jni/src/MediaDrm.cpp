/*
 *  Copyright (C) 2016 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
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

  std::vector<char> result;

  if (!env->ExceptionCheck())
  {
    jsize size = env->GetArrayLength(array.get());
    result.resize(size);
    env->GetByteArrayRegion(array.get(), 0, size, (jbyte*)result.data());
  }

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

std::vector<uint8_t> CJNIMediaDrm::getPropertyByteArray(const std::string &propertyName) const
{
  JNIEnv *env = xbmc_jnienv();
  jhbyteArray array = call_method<jhbyteArray>(m_object,
    "getPropertyByteArray", "(Ljava/lang/String;)[B",
      jcast<jhstring>(propertyName));

  std::vector<uint8_t> result;

  if (!env->ExceptionCheck())
  {
    jsize size = env->GetArrayLength(array.get());
    result.resize(size);
    env->GetByteArrayRegion(array.get(), 0, size, (jbyte*)result.data());
  }
  return result;
}

void CJNIMediaDrm::setPropertyString(const std::string &propertyName, const std::string &value) const
{
  call_method<void>(m_object,
    "setPropertyString", "(Ljava/lang/String;Ljava/lang/String;)V",
    jcast<jhstring>(propertyName), jcast<jhstring>(value));
}

void CJNIMediaDrm::setPropertyByteArray(const std::string &propertyName, const std::vector<uint8_t> &value) const
{
  JNIEnv* env = xbmc_jnienv();
  call_method<void>(m_object, "setPropertyByteArray", "(Ljava/lang/String;[B)V",
                    jcast<jhstring>(propertyName), jcast<jhbyteArray, std::vector<uint8_t>>(value));
}

CJNIMediaDrmKeyRequest CJNIMediaDrm::getKeyRequest(
    const std::vector<char>& scope,
    const std::vector<uint8_t>& init,
    const std::string& mimeType,
    int keyType,
    const std::map<std::string, std::string>& optionalParameters) const
{
  JNIEnv *env = xbmc_jnienv();

  CJNIHashMap hashMap;
  for (const auto &item : optionalParameters)
    hashMap.put(jcast<jhstring>(item.first), jcast<jhstring>(item.second));

  CJNIMediaDrmKeyRequest result =
    call_method<jhobject>(m_object,
      "getKeyRequest", "([B[BLjava/lang/String;ILjava/util/HashMap;)Landroid/media/MediaDrm$KeyRequest;",
      jcast<jhbyteArray, std::vector<char>>(scope), jcast<jhbyteArray, std::vector<uint8_t>>(init),
      jcast<jhstring>(mimeType), keyType, hashMap.get_raw());

  return result;
}

std::vector<char> CJNIMediaDrm::provideKeyResponse(const std::vector<char> &scope, const std::vector<char> &response) const
{
  JNIEnv *env = xbmc_jnienv();
  jhbyteArray array = call_method<jhbyteArray>(m_object,
    "provideKeyResponse", "([B[B)[B", jcast<jhbyteArray, std::vector<char> >(scope),
    jcast<jhbyteArray, std::vector<char> >(response));

  std::vector<char> result;

  if (!(!array))
  {
    jsize size = env->GetArrayLength(array.get());
    result.resize(size);
    env->GetByteArrayRegion(array.get(), 0, size, (jbyte*)result.data());
  }
  return result;
}

CJNIMediaDrmProvisionRequest CJNIMediaDrm::getProvisionRequest() const
{
  return call_method<jhobject>(m_object,
      "getProvisionRequest", "()Landroid/media/MediaDrm$ProvisionRequest;");
}

void CJNIMediaDrm::provideProvisionResponse(const std::vector<uint8_t> &response) const
{
  call_method<void>(m_object,
    "provideProvisionResponse", "([B)V", jcast<jhbyteArray, std::vector<uint8_t> >(response));
}

void CJNIMediaDrm::removeKeys(const std::vector<char> &sessionId) const
{
  call_method<void>(m_object,
    "removeKeys", "([B)V", jcast<jhbyteArray, std::vector<char> >(sessionId));
}

void CJNIMediaDrm::setOnEventListener(const CJNIMediaDrmOnEventListener &listener) const
{
  call_method<void>(m_object, "setOnEventListener",
    "(Landroid/media/MediaDrm$OnEventListener;)V",
    listener.get_raw());
}

std::map<std::string, std::string> CJNIMediaDrm::queryKeyStatus(const std::vector<char> &sessionId) const
{
  if (CJNIBase::GetSDKVersion() >= 23)
  {
    std::map<std::string, std::string> result;

    CJNIHashMap hashMap = call_method<jhobject>(m_object,
      "queryKeyStatus", "([B)Ljava/util/HashMap;", jcast<jhbyteArray, std::vector<char> >(sessionId));
    // Get a set with Map.entry from hashmap
    jhobject entrySet = hashMap.entrySet();
    // Get the Iterator
    jhobject iterator =  call_method<jhobject>(entrySet, "iterator", "()Ljava/util/Iterator;");
    while (call_method<jboolean>(iterator, "hasNext", "()Z"))
    {
      jhobject next = call_method<jhobject>(iterator, "next", "()Ljava/util/Map$Entry;");
      std::string key = jcast<std::string>(call_method<jhstring>(next, "getKey", "()Ljava/lang/Object;"));
      std::string value = jcast<std::string>(call_method<jhstring>(next, "getValue", "()Ljava/lang/Object;"));
      result[key] = value;
    }
    return result;
  }
  return std::map<std::string, std::string>();
}

int CJNIMediaDrm::getSecurityLevel(const std::vector<char> &sessionId) const
{
  if (CJNIBase::GetSDKVersion() >= 28)
  {
    return call_method<int>(m_object,
      "getSecurityLevel", "([B)I", jcast<jhbyteArray, std::vector<char> >(sessionId));
  }
  return -1;
}


int CJNIMediaDrm::getMaxSecurityLevel() const
{
  if (CJNIBase::GetSDKVersion() >= 28)
  {
    return call_static_method<int>(GetClassName().c_str(),
      "getMaxSecurityLevel", "()I");
  }
  return -1;
}
