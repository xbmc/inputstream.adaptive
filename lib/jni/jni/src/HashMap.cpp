/*
 *  Copyright (C) 2016 Christian Browet
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "HashMap.h"
#include "jutils-details.hpp"

using namespace jni;

CJNIHashMap::CJNIHashMap()
  : CJNIBase("java/util/HashMap")
{
  m_object = new_object(GetClassName(), "<init>", "(I)V", 1);
  m_object.setGlobal();
}

jhstring CJNIHashMap::put(const jhstring key, const jhstring value)
{
  return call_method<jhstring>(m_object,
                               "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                               key, value);
}

jhobject CJNIHashMap::entrySet()
{
  return call_method<jhobject>(m_object,
                               "entrySet", "()Ljava/util/Set;");
}
