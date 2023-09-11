/*
 *  Copyright (C) 2013 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../jutils/jutils.hpp"

#include <list>

class CJNIBase
{
  typedef void (CJNIBase::*safe_bool_type)();
  void non_null_object() {}

public:
  operator safe_bool_type() const { return !m_object ?  0 : &CJNIBase::non_null_object; }
  const jni::jhobject& get_raw() const { return m_object; }
  static void SetSDKVersion(int);
  static int GetSDKVersion();
  static void SetBaseClassName(const std::string &className);
  static const std::string &GetBaseClassName();
  const static std::string ExceptionToString();

  static int RESULT_OK;
  static int RESULT_CANCELED;

protected:
  CJNIBase() {}
  CJNIBase(jni::jhobject const& object);
  CJNIBase(std::string classname);
  virtual ~CJNIBase();

  const std::string & GetClassName() const {return m_className;}
  static const std::string GetDotClassName(const std::string & classname);

  jni::jhobject m_object;

private:
  std::string m_className;
  static int m_sdk_version;
  static std::string m_baseClassName;
};

template <typename I>
class CJNIInterfaceImplem
{
protected:
  static std::list<std::pair<jni::jhobject, I*>> s_object_map;  

  static void add_instance(const jni::jhobject& o, I* inst)
  {
    s_object_map.push_back(std::pair<jni::jhobject, I*>(o, inst));
  }

  static I* find_instance(const jobject& o)
  {
    for( auto it = s_object_map.begin(); it != s_object_map.end(); ++it )
    {
      if (it->first == o)
        return it->second;
    }
    return nullptr;
  }
  
  static void remove_instance(I* inst)
  {
    for( auto it = s_object_map.begin(); it != s_object_map.end(); ++it )
    {
      if (it->second == inst)
      {
        s_object_map.erase(it);
        break;
      }
    }
  }
};

template <typename I> std::list<std::pair<jni::jhobject, I*>> CJNIInterfaceImplem<I>::s_object_map;  
