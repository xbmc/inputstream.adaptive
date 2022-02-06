/*
 *  Copyright (C) 2013 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "JNIBase.h"
#include "jutils-details.hpp"

#include <algorithm>

using namespace jni;
int CJNIBase::m_sdk_version = -1;
int CJNIBase::RESULT_OK = -1;
int CJNIBase::RESULT_CANCELED = 0;
std::string CJNIBase::m_baseClassName;

CJNIBase::CJNIBase(std::string classname)
{
  // Convert "the.class.name" to "the/class/name"
  m_className = classname;
  std::replace(m_className.begin(), m_className.end(), '.', '/');
}

CJNIBase::CJNIBase(const jhobject &object):
    m_object(object)
{
  m_object.setGlobal();
}

CJNIBase::~CJNIBase()
{
  if(!m_object)
    return;
}

void CJNIBase::SetSDKVersion(int version)
{
  m_sdk_version = version;
}

int CJNIBase::GetSDKVersion()
{
  return m_sdk_version;
}

void CJNIBase::SetBaseClassName(const std::string &classname)
{
  m_baseClassName = classname;
}

const std::string &CJNIBase::GetBaseClassName()
{
  return m_baseClassName;
}

const std::string CJNIBase::GetDotClassName(const std::string & classname)
{
  std::string dotClassName = classname;
  std::replace(dotClassName.begin(), dotClassName.end(), '/', '.');
  return dotClassName;
}

const std::string CJNIBase::ExceptionToString()
{
  JNIEnv* jenv = xbmc_jnienv();
  jhthrowable exception = (jhthrowable)jenv->ExceptionOccurred();
  if (!exception)
    return "";

  jenv->ExceptionClear();
  jhclass excClass = find_class(jenv, "java/lang/Throwable");
  jmethodID toStrMethod = get_method_id(jenv, excClass, "toString", "()Ljava/lang/String;");
  jhstring msg = call_method<jhstring>(exception, toStrMethod);
  return (jcast<std::string>(msg));
}
