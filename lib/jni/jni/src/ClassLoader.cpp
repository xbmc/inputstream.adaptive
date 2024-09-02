/*
 *  Copyright (C) 2013 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ClassLoader.h"
#include "jutils-details.hpp"

using namespace jni;

jni::CJNIClassLoader::CJNIClassLoader(const std::string &dexPath)
  : CJNIBase("dalvik/system/PathClassLoader")
{
  jhobject systemLoader = call_static_method<jhobject>("java/lang/ClassLoader", "getSystemClassLoader", "()Ljava/lang/ClassLoader;");

  m_object = new_object(GetClassName(), "<init>", "(Ljava/lang/String;Ljava/lang/ClassLoader;)V",
    jcast<jhstring>(dexPath), systemLoader);
  m_object.setGlobal();
}

jhclass jni::CJNIClassLoader::loadClass(std::string className) const
{
  return call_method<jhclass>(m_object,
    "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;",
    jcast<jhstring>(className)); 
}
