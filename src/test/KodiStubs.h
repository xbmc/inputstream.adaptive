/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

 // Kodi interface stubs

#include <cstdint>
#include <string>
#include <vector>
#include <map>

#ifdef _WIN32 // windows
#if !defined(_SSIZE_T_DEFINED) && !defined(HAVE_SSIZE_T)
typedef intptr_t ssize_t;
#define _SSIZE_T_DEFINED
#endif // !_SSIZE_T_DEFINED
#ifndef SSIZE_MAX
#define SSIZE_MAX INTPTR_MAX
#endif // !SSIZE_MAX
#else // Linux, Mac, FreeBSD
#include <sys/types.h>
#endif // TARGET_POSIX

#define ATTR_DLL_LOCAL

typedef enum CURLOptiontype
{
  ADDON_CURL_OPTION_OPTION,
  ADDON_CURL_OPTION_PROTOCOL,
  ADDON_CURL_OPTION_CREDENTIALS,
  ADDON_CURL_OPTION_HEADER
} CURLOptiontype;

class CacheStatus;

typedef enum FilePropertyTypes
{
  ADDON_FILE_PROPERTY_RESPONSE_PROTOCOL,
  ADDON_FILE_PROPERTY_RESPONSE_HEADER,
  ADDON_FILE_PROPERTY_CONTENT_TYPE,
  ADDON_FILE_PROPERTY_CONTENT_CHARSET,
  ADDON_FILE_PROPERTY_MIME_TYPE,
  ADDON_FILE_PROPERTY_EFFECTIVE_URL
} FilePropertyTypes;

typedef enum OpenFileFlags
{
  ADDON_READ_TRUNCATED = 0x01,
  ADDON_READ_CHUNKED = 0x02,
  ADDON_READ_CACHED = 0x04,
  ADDON_READ_NO_CACHE = 0x08,
  ADDON_READ_BITRATE = 0x10,
  ADDON_READ_MULTI_STREAM = 0x20,
  ADDON_READ_AUDIO_VIDEO = 0x40,
  ADDON_READ_AFTER_WRITE = 0x80,
  ADDON_READ_REOPEN = 0x100
} OpenFileFlags;

enum AdjustRefreshRateStatus
{
  ADJUST_REFRESHRATE_STATUS_OFF = 0,
  ADJUST_REFRESHRATE_STATUS_ALWAYS,
  ADJUST_REFRESHRATE_STATUS_ON_STARTSTOP,
  ADJUST_REFRESHRATE_STATUS_ON_START,
};

enum INPUTSTREAM_TYPE
{
  INPUTSTREAM_TYPE_NONE = 0,
  INPUTSTREAM_TYPE_VIDEO,
  INPUTSTREAM_TYPE_AUDIO,
  INPUTSTREAM_TYPE_SUBTITLE,
  INPUTSTREAM_TYPE_TELETEXT,
  INPUTSTREAM_TYPE_RDS,
  INPUTSTREAM_TYPE_ID3,
};

namespace kodi
{
namespace addon
{
class InputstreamInfo
{
public:
  std::string GetCodecName() const { return ""; }
  unsigned int GetWidth() const { return 1920; }
  unsigned int GetHeight() const { return 1080; }
};

inline std::string GetAddonInfo(const std::string& id)
{
  return "";
}

inline std::string GetLocalizedString(uint32_t labelId, const std::string& defaultStr = "")
{
  return defaultStr;
}

inline std::string GetSettingString(const std::string& settingName,
                                    const std::string& defaultValue = "")
{
  return defaultValue;
}

inline int GetSettingInt(const std::string& settingName, int defaultValue = 0)
{
  return defaultValue;
}

inline bool GetSettingBoolean(const std::string& settingName, bool defaultValue = false)
{
  return defaultValue;
}

inline std::string GetUserPath(const std::string& append = "")
{
  return "C:\\isa_stub_test\\" + append;
}

} // namespace addon

struct VFSProperty
{
  char* name;
  char* val;
};

struct VFSDirEntry
{
  char* label; //!< item label
  char* title; //!< item title
  char* path; //!< item path
  unsigned int num_props; //!< Number of properties attached to item
  struct VFSProperty* properties; //!< Properties
  time_t date_time; //!< file creation date & time
  bool folder; //!< Item is a folder
  uint64_t size; //!< Size of file represented by item
};

namespace vfs
{
class ATTR_DLL_LOCAL CDirEntry
{
public:
  CDirEntry(const std::string& label = "",
            const std::string& path = "",
            bool folder = false,
            int64_t size = -1,
            time_t dateTime = 0)
    : m_label(label), m_path(path), m_folder(folder), m_size(size), m_dateTime(dateTime)
  {
  }

  explicit CDirEntry(const VFSDirEntry& dirEntry)
    : m_label(dirEntry.label ? dirEntry.label : ""),
      m_path(dirEntry.path ? dirEntry.path : ""),
      m_folder(dirEntry.folder),
      m_size(dirEntry.size),
      m_dateTime(dirEntry.date_time)
  {
  }

  const std::string& Label(void) const { return m_label; }
  const std::string& Title(void) const { return m_title; }
  const std::string& Path(void) const { return m_path; }
  bool IsFolder(void) const { return m_folder; }
  int64_t Size(void) const { return m_size; }
  time_t DateTime() { return m_dateTime; }
  void SetLabel(const std::string& label) { m_label = label; }
  void SetTitle(const std::string& title) { m_title = title; }
  void SetPath(const std::string& path) { m_path = path; }
  void SetFolder(bool folder) { m_folder = folder; }
  void SetSize(int64_t size) { m_size = size; }
  void SetDateTime(time_t dateTime) { m_dateTime = dateTime; }
  void AddProperty(const std::string& id, const std::string& value) { m_properties[id] = value; }
  void ClearProperties() { m_properties.clear(); }
  const std::map<std::string, std::string>& GetProperties() const { return m_properties; }
private:
  std::string m_label;
  std::string m_title;
  std::string m_path;
  std::map<std::string, std::string> m_properties;
  bool m_folder;
  int64_t m_size;
  time_t m_dateTime;
};

class CFile
{
public:
  CFile() = default;
  virtual ~CFile() { Close(); }
  bool OpenFile(const std::string& filename, unsigned int flags = 0) { return false; }

  bool OpenFileForWrite(const std::string& filename, bool overwrite = false) { return false; }

  bool IsOpen() const { return false; }
  void Close() {}

  bool CURLCreate(const std::string& url) { return false; }
  bool CURLAddOption(CURLOptiontype type, const std::string& name, const std::string& value)
  {
    return false;
  }

  bool CURLOpen(unsigned int flags = 0) { return false; }

  ssize_t Read(void* ptr, size_t size) { return 0; }

  bool ReadLine(std::string& line) { return false; }

  ssize_t Write(const void* ptr, size_t size) { return 0; }

  void Flush() {}

  int64_t Seek(int64_t position, int whence = SEEK_SET) { return 0; }

  int Truncate(int64_t size) { return 0; }

  int64_t GetPosition() const { return 0; }

  int64_t GetLength() const { return 0; }

  bool AtEnd() const { return true; }

  int GetChunkSize() const { return 0; }

  bool IoControlGetSeekPossible() const { return false; }

  bool IoControlGetCacheStatus(CacheStatus& status) const { return false; }

  bool IoControlSetCacheRate(uint32_t rate) { return false; }

  bool IoControlSetRetry(bool retry) { return false; }

  const std::string GetPropertyValue(FilePropertyTypes type, const std::string& name) const
  {
    return "";
  }

  const std::vector<std::string> GetPropertyValues(FilePropertyTypes type,
                                                   const std::string& name) const
  {
    return std::vector<std::string>();
  }

  double GetFileDownloadSpeed() const { return 0.0; }
};

inline bool FileExists(const std::string& filename, bool usecache = false)
{
  return false;
}

inline bool DirectoryExists(const std::string& path)
{
  return false;
}

inline bool RemoveDirectory(const std::string& path, bool recursive = false)
{
  return true;
}

inline std::string TranslateSpecialProtocol(const std::string& source)
{
  return "";
}

inline bool GetDirectory(const std::string& path,
                         const std::string& mask,
                         std::vector<kodi::vfs::CDirEntry>& items)
{
  return false;
}

} // namespace vfs

namespace gui
{

inline AdjustRefreshRateStatus GetAdjustRefreshRateStatus()
{
  return AdjustRefreshRateStatus::ADJUST_REFRESHRATE_STATUS_OFF;
}

namespace dialogs
{

namespace Select
{

inline int Show(const std::string& heading,
                const std::vector<std::string>& entries,
                int selected = -1,
                unsigned int autoclose = 0)
{
  return selected;
}

} // namespace Select

} // namespace dialogs

} // namespace gui

} // namespace kodi
