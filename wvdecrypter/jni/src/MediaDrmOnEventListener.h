#pragma once
/*
 *      Copyright (C) 2018 XBMC Foundation
 *      http://kodi.tv
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


class CJNIClassLoader;

namespace jni
{

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

