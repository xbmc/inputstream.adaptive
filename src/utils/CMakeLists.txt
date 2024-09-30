set(SOURCES
  Base64Utils.cpp
  CharArrayParser.cpp
  CurlUtils.cpp
  DigestMD5Utils.cpp
  FileUtils.cpp
  JsonUtils.cpp
  StringUtils.cpp
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
  JsonUtils.h
  log.h
  StringUtils.h
  UrlUtils.h
  Utils.h
  XMLUtils.h
)

add_dir_sources(SOURCES HEADERS)
