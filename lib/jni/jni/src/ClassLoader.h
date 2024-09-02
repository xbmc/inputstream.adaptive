/*
 *  Copyright (C) 2013 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "JNIBase.h"

namespace jni
{
class CJNIClassLoader : public CJNIBase
{
public:
  CJNIClassLoader(const std::string &dexPath);
  CJNIClassLoader(const jni::jhobject &object) : CJNIBase(object) {};
  ~CJNIClassLoader() {};

  jni::jhclass loadClass(std::string className) const;

private:
  CJNIClassLoader();
};
}
