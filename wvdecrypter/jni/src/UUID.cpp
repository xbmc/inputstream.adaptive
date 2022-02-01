/*
 *  Copyright (C) 2016 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "UUID.h"

#include "jutils-details.hpp"

using namespace jni;

CJNIUUID::CJNIUUID(int64_t mostSigBits, int64_t leastSigBits)
  : CJNIBase("java/util/UUID")
{
  m_object = new_object(GetClassName(), "<init>", "(JJ)V", mostSigBits, leastSigBits);
  m_object.setGlobal();
}
