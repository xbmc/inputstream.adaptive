/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "../utils/UrlUtils.h"

#include <gtest/gtest.h>

using namespace UTILS;

TEST(UrlUtilsTest, DetectsResourceSchemes)
{
  EXPECT_TRUE(URL::IsHttpUrl("http://example.test/master.m3u8"));
  EXPECT_TRUE(URL::IsHttpUrl("HTTPS://example.test/master.m3u8"));
  EXPECT_FALSE(URL::IsHttpUrl("file:///tmp/master.m3u8"));

  EXPECT_TRUE(URL::IsUrlAbsolute("file:///tmp/master.m3u8"));
  EXPECT_TRUE(URL::IsUrlAbsolute("special://home/master.m3u8"));
  EXPECT_TRUE(URL::IsUrlAbsolute("magnet:?xt=urn:btih:abc"));
  EXPECT_FALSE(URL::IsUrlAbsolute("video/index.m3u8"));
}

TEST(UrlUtilsTest, ResolvesNestedFileResources)
{
  const std::string master{"file:///media/collisions/master.m3u8"};
  const std::string child{URL::Join(URL::GetUrlPath(master), "video/index.m3u8")};

  EXPECT_EQ(child, "file:///media/collisions/video/index.m3u8");
  EXPECT_EQ(URL::Join(URL::GetUrlPath(child), "init.mp4"),
            "file:///media/collisions/video/init.mp4");
  EXPECT_EQ(URL::Join(URL::GetUrlPath(child), "segment00000.m4s"),
            "file:///media/collisions/video/segment00000.m4s");
  EXPECT_EQ(URL::Join(master, "https://cdn.example/child.m3u8"),
            "https://cdn.example/child.m3u8");
}

TEST(UrlUtilsTest, PreservesMagnetIdentityAcrossNestedResources)
{
  const std::string identity{"?xt=urn:btih:abc&dn=collisions"};
  const std::string master{"magnet://collisions/master.m3u8" + identity};
  const std::string child{URL::Join(URL::GetUrlPath(master), "video/index.m3u8")};

  EXPECT_EQ(child, "magnet://collisions/video/index.m3u8" + identity);
  EXPECT_EQ(URL::Join(URL::GetUrlPath(child), "init.mp4"),
            "magnet://collisions/video/init.mp4" + identity);
  EXPECT_EQ(URL::Join(URL::GetUrlPath(child), "segment.m4s?piece=1#part"),
            "magnet://collisions/video/segment.m4s?piece=1&xt=urn:btih:abc&dn=collisions#part");

  const std::string rootMaster{"magnet://master.m3u8" + identity};
  EXPECT_EQ(URL::Join(URL::GetUrlPath(rootMaster), "video/index.m3u8"),
            "magnet://video/index.m3u8" + identity);
}
