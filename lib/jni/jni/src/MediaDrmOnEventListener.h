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
class CJNIClassLoader;
class CJNIMediaDrm;

class CJNIMediaDrmOnEventListener : public CJNIBase, public CJNIInterfaceImplem<CJNIMediaDrmOnEventListener>
{
public:
  CJNIMediaDrmOnEventListener(const CJNIClassLoader *classLoader);
  explicit CJNIMediaDrmOnEventListener(const jhobject &object) : CJNIBase(object) {}
  virtual ~CJNIMediaDrmOnEventListener();

  static void RegisterNatives(JNIEnv* env);

public:
  virtual void onEvent(const CJNIMediaDrm &mediaDrm, const std::vector<char> &sessionId, int event, int extra, const std::vector<char> &data) = 0;

protected:
  static void _onEvent(JNIEnv* env, jobject thiz, jobject mediaDrm, jbyteArray sessionId, jint event, jint extra, jbyteArray data);
private:
  jhclass m_class;
};

}

