/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <bento4/Ap4.h>
#include <jni/src/MediaDrm.h>
#include <kodi/AddonBase.h>

enum WV_KEYSYSTEM
{
  NONE,
  WIDEVINE,
  PLAYREADY,
  WISEPLAY
};

class CWVDecrypterA;

class ATTR_DLL_LOCAL CWVCdmAdapterA
{
public:
  CWVCdmAdapterA(WV_KEYSYSTEM ks,
              const char* licenseURL,
              const AP4_DataBuffer& serverCert,
              jni::CJNIMediaDrmOnEventListener* listener,
              CWVDecrypterA* host);
  ~CWVCdmAdapterA();

  jni::CJNIMediaDrm* GetMediaDrm() { return m_mediaDrm; };

  const std::string& GetLicenseURL() const { return m_licenseUrl; };

  const uint8_t* GetKeySystem() const
  {
    static const uint8_t keysystemId[3][16] = {
        {0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce, 0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21,
         0xed},
        {0x9A, 0x04, 0xF0, 0x79, 0x98, 0x40, 0x42, 0x86, 0xAB, 0x92, 0xE6, 0x5B, 0xE0, 0x88, 0x5F,
         0x95},
        {0x3d, 0x5e, 0x6d, 0x35, 0x9b, 0x9a, 0x41, 0xe8, 0xb8, 0x43, 0xdd, 0x3c, 0x6e, 0x72, 0xc4,
         0x2c},
    };
    return keysystemId[m_keySystem - 1];
  }
  WV_KEYSYSTEM GetKeySystemType() const { return m_keySystem; };
  void SaveServiceCertificate();

private:
  void LoadServiceCertificate();

  WV_KEYSYSTEM m_keySystem;
  jni::CJNIMediaDrm* m_mediaDrm;
  std::string m_licenseUrl;
  std::string m_strBasePath;
  CWVDecrypterA* m_host;
};
