/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://www.xbmc.org
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

#ifndef TS_MUTEX_H
#define TS_MUTEX_H

#if defined(_MSC_VER)
#include <windows.h>
#define pthread_mutex_init(a, b) InitializeCriticalSection(a)
#define pthread_mutex_destroy(a) DeleteCriticalSection(a)
#define pthread_mutex_lock(a) EnterCriticalSection(a)
#define pthread_mutex_unlock(a) LeaveCriticalSection(a)
typedef CRITICAL_SECTION pthread_mutex_t;
#else
#include <pthread.h>
namespace TSDemux
{
namespace PLATFORM
{
  inline pthread_mutexattr_t *GetRecursiveMutexAttribute(void)
  {
    static pthread_mutexattr_t g_mutexAttr;
    static bool bAttributeInitialised = false;
    if (!bAttributeInitialised)
    {
      pthread_mutexattr_init(&g_mutexAttr);
      pthread_mutexattr_settype(&g_mutexAttr, PTHREAD_MUTEX_RECURSIVE);
      bAttributeInitialised = true;
    }
    return &g_mutexAttr;
  }
}
}
#endif /* _MSC_VER */

namespace TSDemux
{
namespace PLATFORM
{
  class PreventCopy
  {
  public:
    inline PreventCopy(void) {}
    inline ~PreventCopy(void) {}

  private:
    inline PreventCopy(const PreventCopy &c) { *this = c; }
    inline PreventCopy &operator=(const PreventCopy &c){ *this = c; return *this; }
  };

  class CMutex : public PreventCopy
  {
  public:
    inline CMutex(void)
    {
      pthread_mutex_init(&m_mutex, GetRecursiveMutexAttribute());
    }

    inline ~CMutex(void)
    {
      pthread_mutex_destroy(&m_mutex);
    }

    inline void Lock(void)
    {
      pthread_mutex_lock(&m_mutex);
    }

    inline void Unlock(void)
    {
      pthread_mutex_unlock(&m_mutex);
    }

  private:
    pthread_mutex_t m_mutex;
  };

  class CLockObject : public PreventCopy
  {
  public:
    inline CLockObject(CMutex& mutex) :
      m_mutex(mutex)
    {
      m_mutex.Lock();
    }

    inline ~CLockObject(void)
    {
      Unlock();
    }

    inline void Unlock(void)
    {
      m_mutex.Unlock();
    }

    inline void Lock(void)
    {
      m_mutex.Lock();
    }

  private:
    CMutex& m_mutex;
  };
}
}

#endif /* TS_MUTEX_H */

