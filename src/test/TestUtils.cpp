/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TestHelper.h"

#include "../utils/UrlUtils.h"

#include <gtest/gtest.h>

using namespace UTILS;

class UtilsTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
  }

  void TearDown() override
  {
  }
  
};

TEST_F(UtilsTest, DetermineBaseDomain)
{
  std::string url = "https://foo.bar/mpd/test.mpd";
  EXPECT_EQ(URL::GetDomainUrl(url), "https://foo.bar");
}

TEST_F(UtilsTest, JoinUrls)
{
  std::string baseUrl;
  std::string otherUrl;

  baseUrl = "https://foo.bar";
  otherUrl = "ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending");

  otherUrl = "/ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending");

  otherUrl = "../ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending");


  baseUrl = "https://foo.bar";
  otherUrl = "ending/";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending/");

  otherUrl = "/ending/";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending/");

  otherUrl = "../ending/";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending/");


  baseUrl = "https://foo.bar/";
  otherUrl = "ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending");

  otherUrl = "/ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending");

  otherUrl = "../ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending");


  baseUrl = "https://foo.bar/ignoredpart?q=a";
  otherUrl = "ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending");

  otherUrl = "/ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending");

  otherUrl = "../ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending");


  baseUrl = "https://foo.bar/sub/";
  otherUrl = "ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/sub/ending");

  otherUrl = "/ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending");

  otherUrl = "../ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending");


  baseUrl = "https://foo.bar/sub1/sub2/";
  otherUrl = ".ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/sub1/sub2/.ending");

  otherUrl = "./ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/sub1/sub2/ending");

  otherUrl = "././ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/sub1/sub2/ending");


  baseUrl = "https://foo.bar/sub1/sub2/";
  otherUrl = ".";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/sub1/sub2/");

  otherUrl = "..";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/sub1/");

  otherUrl = "./";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/sub1/sub2/");

  // Less common and malformed test cases

  baseUrl = "https://foo.bar/sub1/sub2/";
  otherUrl = "../../../../ending/";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending/");

  otherUrl = "/../ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending");

  otherUrl = "/../ending/thismustberemoved/..";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/ending/");

  otherUrl = "../";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/sub1/");

  otherUrl = "/../";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/");

  baseUrl = "";
  otherUrl = "../../../ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "../../../ending");
}
