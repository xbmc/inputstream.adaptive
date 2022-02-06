/*
 *  Copyright (C) 2016 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "JNIBase.h"

namespace jni
{

class CJNIUUID : public CJNIBase
{
public:
  CJNIUUID(int64_t mostSigBits, int64_t leastSigBits);
  CJNIUUID(const jni::jhobject &object) : CJNIBase(object) {}
  ~CJNIUUID() {}

};

}
