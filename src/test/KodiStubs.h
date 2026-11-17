/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

 // Kodi interface stubs

#include <algorithm>
#include <cstdint>
#include <cstdio>
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
  ADDON_READ_CHUNKED = 0x02, // deprecated
  ADDON_READ_CACHED = 0x04,
  ADDON_READ_NO_CACHE = 0x08,
  ADDON_READ_BITRATE = 0x10,
  ADDON_READ_MULTI_STREAM = 0x20,
  ADDON_READ_AUDIO_VIDEO = 0x40,
  ADDON_READ_AFTER_WRITE = 0x80,
  ADDON_READ_REOPEN = 0x100,
  ADDON_READ_NO_BUFFER = 0x200,
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
struct CFileTestState
{
  bool forceUnknownLength{false};
  bool failSeek{false};
  bool curlCreateResult{false};
  bool curlOpenResult{false};
  bool curlCreateCalled{false};
  bool curlOpenCalled{false};
  bool openFileCalled{false};
  size_t readCalls{0};
  std::string responseProtocol;
  std::string effectiveUrl;
  std::map<std::string, std::string> curlHeaders;
};

inline CFileTestState& GetCFileTestState()
{
  static CFileTestState state;
  return state;
}

inline void ResetCFileTestState()
{
  GetCFileTestState() = {};
}

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
  bool OpenFile(const std::string& filename, unsigned int flags = 0)
  {
    auto& state = GetCFileTestState();
    state.openFileCalled = true;
    Close();
    std::string path{filename};
    if (path.starts_with("file://"))
      path.erase(0, 7);
#ifdef _WIN32
    if (path.size() > 2 && path.front() == '/' && path[2] == ':')
      path.erase(0, 1);
    std::replace(path.begin(), path.end(), '/', '\\');
#endif
    m_file = std::fopen(path.c_str(), "rb");
    return m_file != nullptr;
  }

  bool OpenFileForWrite(const std::string& filename, bool overwrite = false) { return false; }

  bool IsOpen() const { return m_file != nullptr; }
  void Close()
  {
    if (m_file)
    {
      std::fclose(m_file);
      m_file = nullptr;
    }
  }

  bool CURLCreate(const std::string& url)
  {
    auto& state = GetCFileTestState();
    state.curlCreateCalled = true;
    return state.curlCreateResult;
  }
  bool CURLAddOption(CURLOptiontype type, const std::string& name, const std::string& value)
  {
    if (type == ADDON_CURL_OPTION_HEADER)
      GetCFileTestState().curlHeaders[name] = value;
    return true;
  }

  bool CURLOpen(unsigned int flags = 0)
  {
    auto& state = GetCFileTestState();
    state.curlOpenCalled = true;
    return state.curlOpenResult;
  }

  ssize_t Read(void* ptr, size_t size)
  {
    ++GetCFileTestState().readCalls;
    if (!m_file)
      return -1;
    const size_t bytesRead = std::fread(ptr, 1, size, m_file);
    return bytesRead == 0 && std::ferror(m_file) ? -1 : static_cast<ssize_t>(bytesRead);
  }

  bool ReadLine(std::string& line) { return false; }

  ssize_t Write(const void* ptr, size_t size) { return 0; }

  void Flush() {}

  int64_t Seek(int64_t position, int whence = SEEK_SET)
  {
    if (GetCFileTestState().failSeek)
      return -1;
    if (!m_file)
      return -1;
#ifdef _WIN32
    return _fseeki64(m_file, position, whence) == 0 ? _ftelli64(m_file) : -1;
#else
    return fseeko(m_file, position, whence) == 0 ? ftello(m_file) : -1;
#endif
  }

  int Truncate(int64_t size) { return 0; }

  int64_t GetPosition() const
  {
    if (!m_file)
      return -1;
#ifdef _WIN32
    return _ftelli64(m_file);
#else
    return ftello(m_file);
#endif
  }

  int64_t GetLength() const
  {
    if (GetCFileTestState().forceUnknownLength)
      return -1;
    const int64_t position = GetPosition();
    if (position < 0)
      return -1;
#ifdef _WIN32
    if (_fseeki64(m_file, 0, SEEK_END) != 0)
      return -1;
    const int64_t length = _ftelli64(m_file);
    _fseeki64(m_file, position, SEEK_SET);
#else
    if (fseeko(m_file, 0, SEEK_END) != 0)
      return -1;
    const int64_t length = ftello(m_file);
    fseeko(m_file, position, SEEK_SET);
#endif
    return length;
  }

  bool AtEnd() const
  {
    const int64_t position = GetPosition();
    const int64_t length = GetLength();
    return position < 0 || length < 0 || position >= length;
  }

  int GetChunkSize() const { return 0; }

  bool IoControlGetSeekPossible() const { return false; }

  bool IoControlGetCacheStatus(CacheStatus& status) const { return false; }

  bool IoControlSetCacheRate(uint32_t rate) { return false; }

  bool IoControlSetRetry(bool retry) { return false; }

  const std::string GetPropertyValue(FilePropertyTypes type, const std::string& name) const
  {
    const auto& state = GetCFileTestState();
    if (type == ADDON_FILE_PROPERTY_RESPONSE_PROTOCOL)
      return state.responseProtocol;
    if (type == ADDON_FILE_PROPERTY_EFFECTIVE_URL)
      return state.effectiveUrl;
    return "";
  }

  const std::vector<std::string> GetPropertyValues(FilePropertyTypes type,
                                                   const std::string& name) const
  {
    return std::vector<std::string>();
  }

  double GetFileDownloadSpeed() const { return 0.0; }

private:
  std::FILE* m_file{nullptr};
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

namespace OK
{

inline void ShowAndGetInput(const std::string& heading, const std::string& text)
{
}

} // namespace OK

} // namespace dialogs

} // namespace gui

} // namespace kodi
