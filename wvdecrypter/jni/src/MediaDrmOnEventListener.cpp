/*
 *      Copyright (C) 2016 Christian Browet
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

#include "MediaDrmOnEventListener.h"
#include "MediaDrm.h"
#include "ClassLoader.h"
#include "jutils-details.hpp"

using namespace jni;

static std::string s_className =  "/interfaces/XBMCMediaDrmOnEventListener";

CJNIMediaDrmOnEventListener::CJNIMediaDrmOnEventListener(const CJNIClassLoader *classLoader)
  : CJNIBase(GetBaseClassName() + s_className)
{
  jhclass clazz = classLoader->loadClass(GetDotClassName(GetClassName()));

  JNINativeMethod methods[] =
  {
    {"_onEvent", "(Landroid/media/MediaDrm;[BII[B)V", (void*)&CJNIMediaDrmOnEventListener::_onEvent}
  };

  xbmc_jnienv()->RegisterNatives(clazz, methods, sizeof(methods)/sizeof(methods[0]));

  m_object = new_object(clazz);
  m_object.setGlobal();

  add_instance(m_object, this);
}

CJNIMediaDrmOnEventListener::~CJNIMediaDrmOnEventListener()
{
  remove_instance(this);
}

void CJNIMediaDrmOnEventListener::_onEvent(JNIEnv* env, jobject thiz, jobject mediaDrm, jbyteArray sessionId, jint event, jint extra, jbyteArray data)
{
  CJNIMediaDrmOnEventListener *inst = find_instance(thiz);
  if (inst)
    inst->onEvent(CJNIMediaDrm(jhobject::fromJNI(mediaDrm)),
      jcast<std::vector<char> >(sessionId),
      event,
      extra,
      jcast<std::vector<char> >(data));
}
