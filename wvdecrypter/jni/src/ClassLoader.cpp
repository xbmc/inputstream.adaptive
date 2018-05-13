/*
 *      Copyright (C) 2013 Team XBMC
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

#include "ClassLoader.h"
#include "jutils-details.hpp"

using namespace jni;

CJNIClassLoader::CJNIClassLoader(const std::string &dexPath)
  : CJNIBase("dalvik/system/PathClassLoader")
{
  jhobject systemLoader = call_static_method<jhobject>("java/lang/ClassLoader", "getSystemClassLoader", "()Ljava/lang/ClassLoader;");

  m_object = new_object(GetClassName(), "<init>", "(Ljava/lang/String;Ljava/lang/ClassLoader;)V",
    jcast<jhstring>(dexPath), systemLoader);
  m_object.setGlobal();
}

jhclass CJNIClassLoader::loadClass(std::string className) const
{
  return call_method<jhclass>(m_object,
    "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;",
    jcast<jhstring>(className)); 
}
