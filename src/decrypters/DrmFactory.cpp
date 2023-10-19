/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DrmFactory.h"

#include <kodi/addon-instance/inputstream/StreamCrypto.h>
#if ANDROID
#include "widevineandroid/WVDecrypter.h"
#else
#ifndef TARGET_DARWIN_EMBEDDED
#include "widevine/WVDecrypter.h"
#endif
#endif

#if _WIN32
#include <SdkDdkVer.h>
#if WDK_NTDDI_VERSION >= NTDDI_WIN10_VB // Windows SDK higher than Windows 20H2
#include "mediafoundation/MFDecrypter.h"
#endif
#endif

using namespace DRM;

IDecrypter* DRM::FACTORY::GetDecrypter(STREAM_CRYPTO_KEY_SYSTEM keySystem)
{
  if (keySystem == STREAM_CRYPTO_KEY_SYSTEM_WIDEVINE)
  {
#if ANDROID
    return new CWVDecrypterA();
#else
// Darwin embedded are apple platforms different than MacOS (e.g. IOS)
#ifndef TARGET_DARWIN_EMBEDDED
    return new CWVDecrypter();
#endif
#endif
  }
  else if (keySystem == STREAM_CRYPTO_KEY_SYSTEM_PLAYREADY)
  {
#if ANDROID
    return new CWVDecrypterA();
#elif _WIN32 && WDK_NTDDI_VERSION >= NTDDI_WIN10_VB
    return new CMFDecrypter();
#endif
  } 
  else if (keySystem == STREAM_CRYPTO_KEY_SYSTEM_WISEPLAY)
  {
#if ANDROID
    return new CWVDecrypterA();
#endif
  }

  return nullptr;
}
