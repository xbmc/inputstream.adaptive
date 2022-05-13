#pragma once
/*
 *      Copyright (C) 2018 XBMC Foundation
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

#include "JNIBase.h"

namespace jni
{

class CJNIHashMap : public CJNIBase
{
public:
  CJNIHashMap(const jni::jhobject &object) : CJNIBase(object) {}
  CJNIHashMap();
  virtual ~CJNIHashMap() {}

  jhstring put(const jhstring key, const jhstring value);
  jhobject entrySet();
};

}

