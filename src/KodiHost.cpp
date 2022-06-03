/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "KodiHost.h"

#include "utils/log.h"

void* CKodiHost::CURLCreate(const char* strURL)
{
  kodi::vfs::CFile* file{new kodi::vfs::CFile};
  if (!file->CURLCreate(strURL))
  {
    delete file;
    return nullptr;
  }
  return file;
}

bool CKodiHost::CURLAddOption(void* file, CURLOPTIONS opt, const char* name, const char* value)
{
  const CURLOptiontype xbmcmap[]{ADDON_CURL_OPTION_PROTOCOL, ADDON_CURL_OPTION_HEADER};
  return static_cast<kodi::vfs::CFile*>(file)->CURLAddOption(xbmcmap[opt], name, value);
}

const char* CKodiHost::CURLGetProperty(void* file, CURLPROPERTY prop, const char* name)
{
  const FilePropertyTypes xbmcmap[]{ADDON_FILE_PROPERTY_RESPONSE_HEADER};
  m_strPropertyValue = static_cast<kodi::vfs::CFile*>(file)->GetPropertyValue(xbmcmap[prop], name);
  return m_strPropertyValue.c_str();
}

bool CKodiHost::CURLOpen(void* file)
{
  return static_cast<kodi::vfs::CFile*>(file)->CURLOpen(ADDON_READ_NO_CACHE);
}

size_t CKodiHost::ReadFile(void* file, void* lpBuf, size_t uiBufSize)
{
  return static_cast<kodi::vfs::CFile*>(file)->Read(lpBuf, uiBufSize);
}

void CKodiHost::CloseFile(void* file)
{
  return static_cast<kodi::vfs::CFile*>(file)->Close();
}

bool CKodiHost::CreateDir(const char* dir)
{
  return kodi::vfs::CreateDirectory(dir);
}

void CKodiHost::LogVA(const SSD::SSDLogLevel level, const char* format, va_list args)
{
  std::vector<char> data;
  data.resize(256);

  va_list argsStart;
  va_copy(argsStart, args);

  int ret;
  while (static_cast<size_t>(ret = vsnprintf(data.data(), data.size(), format, args)) > data.size())
  {
    data.resize(data.size() * 2);
    args = argsStart;
  }
  LOG::Log(static_cast<LogLevel>(level), data.data());
  va_end(argsStart);
}

void CKodiHost::SetLibraryPath(const char* libraryPath)
{
  m_strLibraryPath = libraryPath;

  const char* pathSep{libraryPath[0] && libraryPath[1] == ':' && isalpha(libraryPath[0]) ? "\\"
                                                                                         : "/"};

  if (m_strLibraryPath.size() && m_strLibraryPath.back() != pathSep[0])
    m_strLibraryPath += pathSep;
}

void CKodiHost::SetProfilePath(const std::string& profilePath)
{
  m_strProfilePath = profilePath;

  const char* pathSep{profilePath[0] && profilePath[1] == ':' && isalpha(profilePath[0]) ? "\\"
                                                                                         : "/"};

  if (m_strProfilePath.size() && m_strProfilePath.back() != pathSep[0])
    m_strProfilePath += pathSep;

  //let us make cdm userdata out of the addonpath and share them between addons
  m_strProfilePath.resize(m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 2));
  m_strProfilePath.resize(m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 1));
  m_strProfilePath.resize(m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 1) +
                          1);

  kodi::vfs::CreateDirectory(m_strProfilePath.c_str());
  m_strProfilePath += "cdm";
  m_strProfilePath += pathSep;
  kodi::vfs::CreateDirectory(m_strProfilePath.c_str());
}

bool CKodiHost::GetBuffer(void* instance, SSD::SSD_PICTURE& picture)
{
  return instance ? static_cast<kodi::addon::CInstanceVideoCodec*>(instance)->GetFrameBuffer(
                        *reinterpret_cast<VIDEOCODEC_PICTURE*>(&picture))
                  : false;
}

void CKodiHost::ReleaseBuffer(void* instance, void* buffer)
{
  if (instance)
    static_cast<kodi::addon::CInstanceVideoCodec*>(instance)->ReleaseFrameBuffer(buffer);
}
