/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MemUtils.h"

#include <cstdlib>
#include <cstring>
#include <limits.h>

#if WIN32
#define HAVE_ALIGNED_MALLOC 1
#else
#define HAVE_POSIX_MEMALIGN 1
#endif

namespace
{
constexpr size_t MAX_ALLOC_SIZE{INT_MAX};
constexpr size_t ALIGN{16};
} // unnamed namespace

// This code has been adapted from av_malloc: https://github.com/FFmpeg/FFmpeg/blob/release/4.4/libavutil/mem.c
void* UTILS::MEMORY::AlignedMalloc(size_t size)
{
  void* ptr = nullptr;

  if (size > MAX_ALLOC_SIZE)
    return nullptr;

#if HAVE_POSIX_MEMALIGN
  if (size) //OS X on SDK 10.6 has a broken posix_memalign implementation
    if (posix_memalign(&ptr, ALIGN, size))
      ptr = nullptr;
#elif HAVE_ALIGNED_MALLOC
  ptr = _aligned_malloc(size, ALIGN);
#elif HAVE_MEMALIGN
#ifndef __DJGPP__
  ptr = memalign(ALIGN, size);
#else
  ptr = memalign(size, ALIGN);
#endif
#else
  ptr = malloc(size);
#endif
  if (!ptr && !size)
  {
    size = 1;
    ptr = AlignedMalloc(1);
  }
  return ptr;
}

void UTILS::MEMORY::AlignedFree(void* ptr)
{
#if HAVE_ALIGNED_MALLOC
  _aligned_free(ptr);
#else
  free(ptr);
#endif
}
