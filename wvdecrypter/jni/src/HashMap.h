/*
 *  Copyright (C) 2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

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

