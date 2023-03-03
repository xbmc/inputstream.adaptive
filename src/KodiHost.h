/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "SSD_dll.h"

#include <kodi/Filesystem.h>
#include <kodi/General.h>
#include <kodi/addon-instance/VideoCodec.h>

#if defined(ANDROID)
#include <kodi/platform/android/System.h>
#endif

class ATTR_DLL_LOCAL CKodiHost : public SSD::SSD_HOST
{
public:
#if defined(ANDROID)
  virtual void* GetJNIEnv() override { return m_androidSystem.GetJNIEnv(); };

  virtual int GetSDKVersion() override { return m_androidSystem.GetSDKVersion(); };

  virtual const char* GetClassName() override
  {
    m_retvalHelper = m_androidSystem.GetClassName();
    return m_retvalHelper.c_str();
  };

#endif
  virtual const char* GetLibraryPath() const override { return m_strLibraryPath.c_str(); };

  virtual const char* GetProfilePath() const override { return m_strProfilePath.c_str(); };

  virtual void* CURLCreate(const char* strURL) override;

  virtual bool CURLAddOption(void* file,
                             CURLOPTIONS opt,
                             const char* name,
                             const char* value) override;

  virtual const char* CURLGetProperty(void* file, CURLPROPERTY prop, const char* name) override;

  virtual bool CURLOpen(void* file) override;

  virtual size_t ReadFile(void* file, void* lpBuf, size_t uiBufSize) override;

  virtual void CloseFile(void* file) override;

  virtual bool CreateDir(const char* dir) override;

  void LogVA(const SSD::SSDLogLevel level, const char* format, va_list args) override;

  void SetLibraryPath(const char* libraryPath);

  void SetProfilePath(const std::string& profilePath);

  virtual bool GetBuffer(void* instance, SSD::SSD_PICTURE& picture) override;

  virtual void ReleaseBuffer(void* instance, void* buffer) override;

  void SetDebugSaveLicense(bool isDebugSaveLicense) override
  {
    m_isDebugSaveLicense = isDebugSaveLicense;
  }

  bool IsDebugSaveLicense() override { return m_isDebugSaveLicense; }

private:
  std::string m_strProfilePath;
  std::string m_strLibraryPath;
  std::string m_strPropertyValue;
  bool m_isDebugSaveLicense;

#if defined(ANDROID)
  kodi::platform::CInterfaceAndroidSystem m_androidSystem;
  std::string m_retvalHelper;
#endif
};
