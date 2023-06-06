/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TestHelper.h"

#include "../common/AdaptiveTreeFactory.h"
#include "../utils/UrlUtils.h"

#include <gtest/gtest.h>

using namespace adaptive;
using namespace PLAYLIST_FACTORY;
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

TEST_F(UtilsTest, AdaptiveTreeFactory_DASH)
{
  PROPERTIES::ManifestType type;
  std::string testDataRegular;
  testHelper::LoadFile("mpd/treefactory_test_regular.mpd", testDataRegular);

  // just uncommon url (e.g. proxy) must fails
  type = InferManifestType("localhost/proxy/getmanifest", "", "");
  EXPECT_EQ(type, PROPERTIES::ManifestType::UNKNOWN);

  // test data parsing
  type = InferManifestType("localhost/proxy/getmanifest", "", testDataRegular);
  EXPECT_EQ(type, PROPERTIES::ManifestType::MPD);

  // test header
  type = InferManifestType("localhost/proxy/getmanifest", "application/dash+xml", "");
  EXPECT_EQ(type, PROPERTIES::ManifestType::MPD);

  // test url
  type = InferManifestType("http://www.someservice.com/cdm1/manifest.mpd", "", "");
  EXPECT_EQ(type, PROPERTIES::ManifestType::MPD);
}

TEST_F(UtilsTest, AdaptiveTreeFactory_HLS)
{
  PROPERTIES::ManifestType type;
  std::string testDataRegular;
  testHelper::LoadFile("hls/treefactory_test_regular.m3u8", testDataRegular);

  // test data parsing
  type = InferManifestType("localhost/proxy/getmanifest", "", testDataRegular);
  EXPECT_EQ(type, PROPERTIES::ManifestType::HLS);

  // test header
  type = InferManifestType("localhost/proxy/getmanifest", "vnd.apple.mpegurl", "");
  EXPECT_EQ(type, PROPERTIES::ManifestType::HLS);

  // test header
  type = InferManifestType("localhost/proxy/getmanifest", "application/vnd.apple.mpegurl", "");
  EXPECT_EQ(type, PROPERTIES::ManifestType::HLS);

  // test header
  type = InferManifestType("localhost/proxy/getmanifest", "application/x-mpegURL", "");
  EXPECT_EQ(type, PROPERTIES::ManifestType::HLS);

  // test url
  type = InferManifestType("http://www.someservice.com/cdm1/manifest.m3u8", "", "");
  EXPECT_EQ(type, PROPERTIES::ManifestType::HLS);
}

TEST_F(UtilsTest, AdaptiveTreeFactory_ISM)
{
  PROPERTIES::ManifestType type;
  std::string testDataUTF8;
  std::string testDataUTF16leBOM;
  testHelper::LoadFile("ism/treefactory_test_utf8.ism", testDataUTF8);
  testHelper::LoadFile("ism/treefactory_test_utf16leBOM.ism", testDataUTF16leBOM);

  // test UTF8 data parsing
  type = InferManifestType("localhost/proxy/getmanifest", "", testDataUTF8);
  EXPECT_EQ(type, PROPERTIES::ManifestType::ISM);

  // test UTF16 LE with BOM data parsing
  type = InferManifestType("localhost/proxy/getmanifest", "", testDataUTF16leBOM);
  EXPECT_EQ(type, PROPERTIES::ManifestType::ISM);

  // test header
  type = InferManifestType("localhost/proxy/getmanifest", "application/vnd.ms-sstr+xml", "");
  EXPECT_EQ(type, PROPERTIES::ManifestType::ISM);

  // test url
  type = InferManifestType("http://www.someservice.com/cdm1/manifest.ism/Manifest", "", "");
  EXPECT_EQ(type, PROPERTIES::ManifestType::ISM);

  // test url
  type = InferManifestType("http://www.someservice.com/cdm1/manifest.isml/Manifest", "", "");
  EXPECT_EQ(type, PROPERTIES::ManifestType::ISM);

  // test url
  type = InferManifestType("http://www.someservice.com/cdm1/manifest.ism", "", "");
  EXPECT_EQ(type, PROPERTIES::ManifestType::ISM);

  // test url
  type = InferManifestType("http://www.someservice.com/cdm1/manifest.isml", "", "");
  EXPECT_EQ(type, PROPERTIES::ManifestType::ISM);
}
