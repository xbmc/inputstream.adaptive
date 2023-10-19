// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <propidl.h>
#include <cassert>

/*!
 * \brief A MS PROPVARIANT that is automatically initialized and cleared upon respective
 * construction and destruction of this class.
*/
class ScopedPropVariant {
public:
    ScopedPropVariant() { PropVariantInit(&pv_); }

    ScopedPropVariant(const ScopedPropVariant&) = delete;
    ScopedPropVariant& operator=(const ScopedPropVariant&) = delete;

    ~ScopedPropVariant() { Reset(); }

    /*!
     *  \brief Returns a pointer to the underlying PROPVARIANT.
     *  Example: Use as an out param in a function call.
     */
    PROPVARIANT* Receive() {
        assert(pv_.vt == VT_EMPTY);
        return &pv_;
    }

    /*!
     * \brief Clears the instance to prepare it for re-use (e.g., via Receive).
     */
    void Reset() {
        if (pv_.vt != VT_EMPTY) {
            HRESULT result = PropVariantClear(&pv_);
            assert(result == S_OK);
        }
    }

    [[nodiscard]] const PROPVARIANT& get() const { return pv_; }
    [[nodiscard]] const PROPVARIANT* ptr() const { return &pv_; }

private:
    PROPVARIANT pv_;
};
