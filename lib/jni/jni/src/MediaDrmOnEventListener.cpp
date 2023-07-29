/*
 *  Copyright (C) 2016 Christian Browet
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
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
