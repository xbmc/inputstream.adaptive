/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <propidl.h>
#include <cassert>

namespace UTILS
{

/*!
 * \brief A MS PROPVARIANT that is automatically initialized and cleared
 * upon respective construction and destruction of this class.
*/
class ScopedPropVariant
{
public:
  ScopedPropVariant() { PropVariantInit(&pv_); }

  ScopedPropVariant(const ScopedPropVariant&) = delete;
  ScopedPropVariant& operator=(const ScopedPropVariant&) = delete;

  ~ScopedPropVariant() { Reset(); }

  /*!
     * \brief Clears the instance & prepares it for re-use (e.g., via Receive).
     */
  void Reset()
  {
    if (pv_.vt == VT_EMPTY)
      return;

    HRESULT result = PropVariantClear(&pv_);
    assert(result == S_OK);
  }

  inline PROPVARIANT* operator->() { return &pv_; }

  const PROPVARIANT& get() const
  {
    assert(pv_.vt == VT_EMPTY);
    return pv_;
  }

  /*!
     * \brief Returns a pointer to the underlying PROPVARIANT.
     * Example: Use as an out param in a function call.
     */
  PROPVARIANT* ptr()
  {
    assert(pv_.vt == VT_EMPTY);
    return &pv_;
  }

  PROPVARIANT release() noexcept
  {
    PROPVARIANT value(pv_);
    PropVariantInit(&pv_);
    return value;
  }

  const PROPVARIANT* ptr() const
  {
    assert(pv_.vt == VT_EMPTY);
    return &pv_;
  }

private:
  PROPVARIANT pv_;
};

} // namespace UTILS
