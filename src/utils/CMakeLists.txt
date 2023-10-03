set(SOURCES
  Base64Utils.cpp
  CharArrayParser.cpp
  CurlUtils.cpp
  DigestMD5Utils.cpp
  FileUtils.cpp
  PropertiesUtils.cpp
  StringUtils.cpp
  SettingsUtils.cpp
  UrlUtils.cpp
  Utils.cpp
  XMLUtils.cpp
)

set(HEADERS
  Base64Utils.h
  CharArrayParser.h
  CryptoUtils.h
  CurlUtils.h
  DigestMD5Utils.h
  FileUtils.h
  log.h
  PropertiesUtils.h
  SettingsUtils.h
  StringUtils.h
  UrlUtils.h
  Utils.h
  XMLUtils.h
)

add_dir_sources(SOURCES HEADERS)
