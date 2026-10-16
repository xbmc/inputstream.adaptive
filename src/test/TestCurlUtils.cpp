/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "../utils/CurlUtils.h"
#include "../SrvBroker.h"

#include <array>
#include <cstdlib>
#include <filesystem>

#include <gtest/gtest.h>

using namespace UTILS;

namespace
{
class StubStateGuard
{
public:
  StubStateGuard()
  {
    kodi::vfs::ResetCFileTestState();
    CSrvBroker::GetInstance()->Initialize();
  }
  ~StubStateGuard()
  {
    CSrvBroker::GetInstance()->Deinitialize();
    kodi::vfs::ResetCFileTestState();
  }
};

std::string GetFixtureUrl()
{
  std::filesystem::path path{std::getenv("DATADIR")};
  path /= "resources/bytes.txt";
  std::string url{std::filesystem::absolute(path).lexically_normal().generic_string()};
  return url.starts_with('/') ? "file://" + url : "file:///" + url;
}
} // unnamed namespace

TEST(CurlUtilsTest, DownloadsVfsFile)
{
  CURL::HTTPResponse response;

  ASSERT_TRUE(CURL::DownloadFile(GetFixtureUrl(), {}, {}, response));
  EXPECT_EQ(response.data, "0123456789abcdefghijklmnopqrstuvwxyz\n");
  EXPECT_EQ(response.dataSize, 37U);
  EXPECT_EQ(response.effectiveUrl, GetFixtureUrl());
  EXPECT_EQ(response.downloadSpeed, 0.0);
}

TEST(CurlUtilsTest, ReadsBoundedVfsRange)
{
  CURL::CUrl resource{GetFixtureUrl()};
  resource.SetByteRange(10, 19);
  ASSERT_EQ(resource.Open(), 200);

  std::string data;
  EXPECT_EQ(resource.Read(data, 3), CURL::ReadStatus::IS_EOF);
  EXPECT_EQ(data, "abcdefghij");
  EXPECT_EQ(resource.GetTotalByteRead(), 10U);
  EXPECT_TRUE(resource.IsEOF());
}

TEST(CurlUtilsTest, ReadsOpenEndedVfsRange)
{
  CURL::CUrl resource{GetFixtureUrl()};
  resource.SetByteRange(30);
  ASSERT_EQ(resource.Open(), 200);

  std::string data;
  EXPECT_EQ(resource.Read(data), CURL::ReadStatus::IS_EOF);
  EXPECT_EQ(data, "uvwxyz\n");
}

TEST(CurlUtilsTest, ReportsEofAfterShortFinalChunk)
{
  CURL::CUrl resource{GetFixtureUrl()};
  ASSERT_EQ(resource.Open(), 200);

  std::array<char, 64> buffer;
  size_t bytesRead{0};
  EXPECT_EQ(resource.ReadChunk(buffer.data(), buffer.size(), bytesRead),
            CURL::ReadStatus::CHUNK_READ);
  EXPECT_EQ(bytesRead, 37U);
  EXPECT_TRUE(resource.IsEOF());
}

TEST(CurlUtilsTest, RejectsMissingVfsFile)
{
  CURL::HTTPResponse response;
  EXPECT_FALSE(CURL::DownloadFile("file:///definitely/not/present/isa-vfs-test", {}, {}, response));
}

TEST(CurlUtilsTest, ReadsUnknownLengthVfsFileToActualEof)
{
  StubStateGuard guard;
  kodi::vfs::GetCFileTestState().forceUnknownLength = true;

  CURL::CUrl resource{GetFixtureUrl()};
  ASSERT_EQ(resource.Open(), 200);

  const std::array<std::string, 4> expectedChunks{
      "0123456789", "abcdefghij", "klmnopqrst", "uvwxyz\n"};
  std::array<char, 10> buffer;
  size_t bytesRead{0};
  for (size_t index = 0; index < expectedChunks.size(); ++index)
  {
    EXPECT_EQ(resource.ReadChunk(buffer.data(), buffer.size(), bytesRead),
              CURL::ReadStatus::CHUNK_READ);
    EXPECT_EQ(std::string(buffer.data(), bytesRead), expectedChunks[index]);
    EXPECT_FALSE(resource.IsEOF());
  }
  EXPECT_EQ(resource.ReadChunk(buffer.data(), buffer.size(), bytesRead), CURL::ReadStatus::IS_EOF);
  EXPECT_EQ(bytesRead, 0U);
  EXPECT_TRUE(resource.IsEOF());
}

TEST(CurlUtilsTest, PreservesHttpTransportAndRangeHeader)
{
  StubStateGuard guard;
  auto& state = kodi::vfs::GetCFileTestState();
  state.curlCreateResult = true;
  state.curlOpenResult = true;
  state.responseProtocol = "HTTP/1.1 206 Partial Content";
  state.effectiveUrl = "https://cdn.example/final.m4s";

  CURL::CUrl resource{"https://cdn.example/segment.m4s"};
  resource.SetByteRange(10, 19);

  EXPECT_EQ(resource.Open(), 206);
  EXPECT_TRUE(state.curlCreateCalled);
  EXPECT_TRUE(state.curlOpenCalled);
  EXPECT_FALSE(state.openFileCalled);
  EXPECT_EQ(resource.GetEffectiveUrl(), state.effectiveUrl);

  ASSERT_EQ(state.curlHeaders.count("Range"), 1U);
  EXPECT_EQ(state.curlHeaders.at("Range"), "bytes=10-19");
}

TEST(CurlUtilsTest, RejectsFailedVfsRangeSeek)
{
  StubStateGuard guard;
  kodi::vfs::GetCFileTestState().failSeek = true;

  CURL::CUrl resource{GetFixtureUrl()};
  resource.SetByteRange(1);
  EXPECT_EQ(resource.Open(), -1);
}
