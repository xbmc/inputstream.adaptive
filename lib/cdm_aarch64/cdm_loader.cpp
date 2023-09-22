/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cstdint>

extern "C"
{
// Linux arm64 version of libwidevinecdm.so depends on two dynamic symbols.
// See https://github.com/xbmc/inputstream.adaptive/issues/1128
#if defined(__linux__) && (defined(__aarch64__) || defined(__arm64__))

  __attribute__((target("no-outline-atomics"))) int32_t __aarch64_ldadd4_acq_rel(int32_t value,
                                                                                 int32_t* ptr)
  {
    return __atomic_fetch_add(ptr, value, __ATOMIC_ACQ_REL);
  }

  __attribute__((target("no-outline-atomics"))) int32_t __aarch64_swp4_acq_rel(int32_t value,
                                                                               int32_t* ptr)
  {
    return __atomic_exchange_n(ptr, value, __ATOMIC_ACQ_REL);
  }
#endif
}
