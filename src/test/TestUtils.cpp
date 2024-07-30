/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TestHelper.h"

#include "../common/AdaptiveTreeFactory.h"
#include "../common/SegTemplate.h"
#include "../utils/DigestMD5Utils.h"
#include "../utils/StringUtils.h"
#include "../utils/UrlUtils.h"
#include "../utils/XMLUtils.h"

#include <gtest/gtest.h>

using namespace adaptive;
using namespace PLAYLIST;
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
  std::string url = "https://www.foo.bar/mpd/test.mpd";
  EXPECT_EQ(URL::GetBaseDomain(url), "https://www.foo.bar");

  url = "https://www.foo.bar/mpd/test.mpd?ping=pong";
  EXPECT_EQ(URL::GetBaseDomain(url), "https://www.foo.bar");

  url = "https://www.foo.bar:1234";
  EXPECT_EQ(URL::GetBaseDomain(url), "https://www.foo.bar");

  url = "https://www.foo.bar:1234/mpd/test.mpd?ping=pong";
  EXPECT_EQ(URL::GetBaseDomain(url), "https://www.foo.bar");
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
  adaptive::TreeType type;
  std::string testDataRegular;
  testHelper::LoadFile("mpd/treefactory_test_regular.mpd", testDataRegular);

  // just uncommon url (e.g. proxy) must fails
  type = InferManifestType("localhost/proxy/getmanifest", "", "");
  EXPECT_EQ(type, adaptive::TreeType::UNKNOWN);

  // test data parsing
  type = InferManifestType("localhost/proxy/getmanifest", "", testDataRegular);
  EXPECT_EQ(type, adaptive::TreeType::DASH);

  // test header
  type = InferManifestType("localhost/proxy/getmanifest", "application/dash+xml", "");
  EXPECT_EQ(type, adaptive::TreeType::DASH);

  // test url
  type = InferManifestType("http://www.someservice.com/cdm1/manifest.mpd", "", "");
  EXPECT_EQ(type, adaptive::TreeType::DASH);
}

TEST_F(UtilsTest, AdaptiveTreeFactory_HLS)
{
  adaptive::TreeType type;
  std::string testDataRegular;
  testHelper::LoadFile("hls/treefactory_test_regular.m3u8", testDataRegular);

  // test data parsing
  type = InferManifestType("localhost/proxy/getmanifest", "", testDataRegular);
  EXPECT_EQ(type, adaptive::TreeType::HLS);

  // test header
  type = InferManifestType("localhost/proxy/getmanifest", "vnd.apple.mpegurl", "");
  EXPECT_EQ(type, adaptive::TreeType::HLS);

  // test header
  type = InferManifestType("localhost/proxy/getmanifest", "application/vnd.apple.mpegurl", "");
  EXPECT_EQ(type, adaptive::TreeType::HLS);

  // test header
  type = InferManifestType("localhost/proxy/getmanifest", "application/x-mpegURL", "");
  EXPECT_EQ(type, adaptive::TreeType::HLS);

  // test url
  type = InferManifestType("http://www.someservice.com/cdm1/manifest.m3u8", "", "");
  EXPECT_EQ(type, adaptive::TreeType::HLS);
}

TEST_F(UtilsTest, AdaptiveTreeFactory_ISM)
{
  adaptive::TreeType type;
  std::string testDataUTF8;
  std::string testDataUTF16leBOM;
  testHelper::LoadFile("ism/treefactory_test_utf8.ism", testDataUTF8);
  testHelper::LoadFile("ism/treefactory_test_utf16leBOM.ism", testDataUTF16leBOM);

  // test UTF8 data parsing
  type = InferManifestType("localhost/proxy/getmanifest", "", testDataUTF8);
  EXPECT_EQ(type, adaptive::TreeType::SMOOTH_STREAMING);

  // test UTF16 LE with BOM data parsing
  type = InferManifestType("localhost/proxy/getmanifest", "", testDataUTF16leBOM);
  EXPECT_EQ(type, adaptive::TreeType::SMOOTH_STREAMING);

  // test header
  type = InferManifestType("localhost/proxy/getmanifest", "application/vnd.ms-sstr+xml", "");
  EXPECT_EQ(type, adaptive::TreeType::SMOOTH_STREAMING);

  // test url
  type = InferManifestType("http://www.someservice.com/cdm1/manifest.ism/Manifest", "", "");
  EXPECT_EQ(type, adaptive::TreeType::SMOOTH_STREAMING);

  // test url
  type = InferManifestType("http://www.someservice.com/cdm1/manifest.isml/Manifest", "", "");
  EXPECT_EQ(type, adaptive::TreeType::SMOOTH_STREAMING);

  // test url
  type = InferManifestType("http://www.someservice.com/cdm1/manifest.ism", "", "");
  EXPECT_EQ(type, adaptive::TreeType::SMOOTH_STREAMING);

  // test url
  type = InferManifestType("http://www.someservice.com/cdm1/manifest.isml", "", "");
  EXPECT_EQ(type, adaptive::TreeType::SMOOTH_STREAMING);
}

TEST_F(UtilsTest, SegTemplateFormatUrlChecks)
{
  CSegmentTemplate segTpl;

  std::string url = "https://cdn.com/example/$$$Number$$RepresentationID$$Bandwidth$$Time$";
  std::string ret = segTpl.FormatUrl(url, "repID", 1500, 1, 0);
  EXPECT_EQ(ret, "https://cdn.com/example/$1repID15000");

  // Dash placeholders use special char "$", but an url can use single "$" char along the path that must be kept
  url = "https://cdn.com/_$_example/QualityLevels($Bandwidth$)/Fragments(video=$Time$)";
  ret = segTpl.FormatUrl(url, "repID", 1500, 1, 0);
  EXPECT_EQ(ret, "https://cdn.com/_$_example/QualityLevels(1500)/Fragments(video=0)");

  // Malformed, do nothing
  url = "https://cdn.com/_$_example/$Bandwidth";
  ret = segTpl.FormatUrl(url, "repID", 1500, 1, 0);
  EXPECT_EQ(ret, "https://cdn.com/_$_example/$Bandwidth");
}

TEST_F(UtilsTest, XMLDateTimeConversions)
{
  EXPECT_EQ(XML::ParseDate("2024-04-30T20:20:13.145433Z"), 1714508413.1454329);

  EXPECT_EQ(XML::ParseDate("2024-05-07T17:00:21"), 1715101221);

  EXPECT_EQ(XML::ParseDate("2024-05-07T17:00:21.989+0200"), 1715101221.989);
}

TEST_F(UtilsTest, MD5HashTest)
{
  std::string strTest = "Test";
  DIGEST::MD5 md5;
  md5.Update(strTest.c_str(), static_cast<uint32_t>(strTest.size()));
  md5.Finalize();
  
  EXPECT_EQ(md5.HexDigest(), "0cbc6611f5540bd0809a388dc95a615b");
}

TEST_F(UtilsTest, UrlEncodeDecode)
{
  const std::string strTest = "abc123-._!()~&%\xC3\xA8\xC3\xB9"; // abc123-._!()~&%ищ
  std::string encoded = STRING::URLEncode(strTest);

  EXPECT_EQ(encoded, "abc123-._!()~%26%25%C3%A8%C3%B9");
  EXPECT_EQ(STRING::URLDecode(encoded), strTest);
}
