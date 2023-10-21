/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include <combaseapi.h>

namespace UTILS
{

/*!
 * \brief Class to automatically release memory allocated in COM. 
 */
template<typename T>
class ScopedCoMem
{
public:
  ScopedCoMem() : m_ptr(nullptr) {}

  ~ScopedCoMem() { Reset(); }

  ScopedCoMem(const ScopedCoMem&) = delete;
  ScopedCoMem& operator=(const ScopedCoMem&) = delete;

  inline T* operator->() { return m_ptr; }
  inline T** operator&() { return &m_ptr; }

  void Reset()
  {
    if (m_ptr)
      CoTaskMemFree(m_ptr);
    m_ptr = nullptr;
  }

  T* get() const { return m_ptr; }

private:
  T* m_ptr;
};

} // namespace UTILS
