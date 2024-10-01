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

class CJNIMediaDrmProvisionRequest : public CJNIBase
{
public:
  CJNIMediaDrmProvisionRequest();
  CJNIMediaDrmProvisionRequest(const jni::jhobject &object) : CJNIBase(object) {};

  std::vector<uint8_t> getData() const;
  std::string getDefaultUrl() const;
};

}

