// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_BASICTYPES_H_
#define BASE_BASICTYPES_H_

#include <limits.h>  // So we can set the bounds of our types.
#include <stddef.h>  // For size_t.
#include <stdint.h>  // For intptr_t.

#include "macros.h"
#include "../build/build_config.h"

// DEPRECATED: Use (u)int{8,16,32,64}_t instead (and include <stdint.h>).
// http://crbug.com/138542
typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;

// DEPRECATED: Use std::numeric_limits (from <limits>) or
// (U)INT{8,16,32,64}_{MIN,MAX} in case of globals (and include <stdint.h>).
// http://crbug.com/138542
const uint64 kuint64max =  0xFFFFFFFFFFFFFFFFULL;
const  int32 kint32max  =  0x7FFFFFFF;
const  int64 kint64max  =  0x7FFFFFFFFFFFFFFFLL;

#endif  // BASE_BASICTYPES_H_
