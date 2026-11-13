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
#include "../decrypters/HelperWv.h"
#include "../utils/Base64Utils.h"
#include "../utils/Bento4Utils.h"
#include "../utils/DigestMD5Utils.h"
#include "../utils/StringUtils.h"
#include "../utils/UrlUtils.h"
#include "../utils/XMLUtils.h"
#include "../utils/Utils.h"

#include <memory>

#include <bento4/Ap4.h>
#include <bento4/Ap4PsshAtom.h>
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
  EXPECT_EQ(URL::GetBaseDomain(url), "https://www.foo.bar:1234");

  url = "https://www.foo.bar:1234/mpd/test.mpd?ping=pong";
  EXPECT_EQ(URL::GetBaseDomain(url), "https://www.foo.bar:1234");

  url = "https://www.foo.bar/example/smil:rtmp.smil/playlist.m3u8?ping=pong";
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


  baseUrl = "https://foo.bar/sub1../sub2./";
  otherUrl = ".ending";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/sub1../sub2./.ending");

  otherUrl = "./.ending/.";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/sub1../sub2./.ending/");

  otherUrl = "./.ending/./";
  EXPECT_EQ(URL::Join(baseUrl, otherUrl), "https://foo.bar/sub1../sub2./.ending/");

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

TEST_F(UtilsTest, AVCC_Conversions)
{
  // AVCCs encoded base64

  // Test 1: Has some extra bytes at the end of NALUs
  const std::string test1AvccB64 = "AWQAIAMBACgnZAAgrQByMBQBbsBEAAADAAQAAAMAy5AAASbMAAAuD/+97gPhAIJwAQAEKP+8sP34+AA=";
  const std::vector<uint8_t> test1annexB = AvcToAnnexb(BASE64::Decode(test1AvccB64));
  EXPECT_EQ(BASE64::Encode(test1annexB), "AAAAASdkACCtAHIwFAFuwEQAAAMABAAAAwDLkAABJswAAC4P/73uA+EAgnAAAAABKP+8sA==");
  const std::vector<uint8_t> test1Avcc = AnnexbToAvc(test1annexB);
  // Due to ignored extra bytes the reconversion cannot match test1AvccB64
  EXPECT_EQ(BASE64::Encode(test1Avcc), "AWQAIP/hACgnZAAgrQByMBQBbsBEAAADAAQAAAMAy5AAASbMAAAuD/+97gPhAIJwAQAEKP+8sA==");
}

namespace
{
// Build a version 1 "sgpd" box with grouping type "seig" and a single entry
std::vector<uint8_t> MakeSeigSgpd(const std::vector<uint8_t>& entry)
{
  std::vector<uint8_t> box;
  auto putU32 = [&box](uint32_t v)
  {
    for (int shift = 24; shift >= 0; shift -= 8)
      box.push_back(static_cast<uint8_t>(v >> shift));
  };
  putU32(static_cast<uint32_t>(8 + 4 + 4 + 4 + 4 + entry.size()));
  box.insert(box.end(), {'s', 'g', 'p', 'd'});
  putU32(0x01000000); // version 1, flags 0
  box.insert(box.end(), {'s', 'e', 'i', 'g'});
  putU32(static_cast<uint32_t>(entry.size())); // default_length
  putU32(1); // entry_count
  box.insert(box.end(), entry.begin(), entry.end());
  return box;
}

std::vector<UTILS::BENTO4::CencSeigGroupEntry> ParseSeig(const std::vector<uint8_t>& entry)
{
  const std::vector<uint8_t> box = MakeSeigSgpd(entry);
  AP4_MemoryByteStream stream(box.data(), static_cast<AP4_Size>(box.size()));
  AP4_Atom* atom{nullptr};
  if (AP4_FAILED(AP4_DefaultAtomFactory::Instance_.CreateAtomFromStream(stream, atom)) || !atom)
    return {};
  std::unique_ptr<AP4_Atom> holder(atom);
  AP4_SgpdAtom* sgpd = AP4_DYNAMIC_CAST(AP4_SgpdAtom, atom);
  if (!sgpd)
    return {};
  return static_cast<UTILS::BENTO4::FMP4SgpdAtom*>(sgpd)->GetSeigEntries();
}

std::vector<uint8_t> MakeKid(uint8_t fill)
{
  return std::vector<uint8_t>(16, fill);
}

const uint8_t WIDEVINE_SYSTEM_ID[16] = {0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce,
                                        0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed};
const uint8_t COMMON_SYSTEM_ID[16] = {0x10, 0x77, 0xef, 0xec, 0xc0, 0xb2, 0x4d, 0x02,
                                      0xac, 0xe3, 0x3c, 0x1e, 0x52, 0xe2, 0xfb, 0x4b};

AP4_PsshAtom* MakePssh(const uint8_t* systemId, const std::vector<uint8_t>& data)
{
  AP4_PsshAtom* pssh = new AP4_PsshAtom(systemId);
  pssh->SetData(data.data(), static_cast<unsigned int>(data.size()));
  return pssh;
}
} // unnamed namespace

TEST_F(UtilsTest, SeigEntrySingleKey)
{
  std::vector<uint8_t> entry = {0x00, 0x19, 0x01, 0x08};
  const std::vector<uint8_t> kid = MakeKid(0xAB);
  entry.insert(entry.end(), kid.begin(), kid.end());

  const auto entries = ParseSeig(entry);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_FALSE(entries[0].multiKeyFlag);
  EXPECT_EQ(entries[0].cryptByteBlock, 1);
  EXPECT_EQ(entries[0].skipByteBlock, 9);
  EXPECT_EQ(entries[0].isProtected, 1);
  ASSERT_EQ(entries[0].keySets.size(), 1u);
  EXPECT_EQ(entries[0].keySets[0].perSampleIvSize, 8);
  EXPECT_EQ(entries[0].keySets[0].kid, kid);
  EXPECT_EQ(entries[0].keySets[0].constantIvSize, 0);
}

TEST_F(UtilsTest, SeigEntryReservedBitsAreIgnored)
{
  std::vector<uint8_t> entry = {0x7F, 0x00, 0x01, 0x10};
  const std::vector<uint8_t> kid = MakeKid(0x11);
  entry.insert(entry.end(), kid.begin(), kid.end());

  const auto entries = ParseSeig(entry);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_FALSE(entries[0].multiKeyFlag);
  ASSERT_EQ(entries[0].keySets.size(), 1u);
  EXPECT_EQ(entries[0].keySets[0].kid, kid);
}

TEST_F(UtilsTest, SeigEntryConstantIv)
{
  std::vector<uint8_t> entry = {0x00, 0x19, 0x01, 0x00};
  const std::vector<uint8_t> kid = MakeKid(0x22);
  const std::vector<uint8_t> iv(16, 0x33);
  entry.insert(entry.end(), kid.begin(), kid.end());
  entry.push_back(16);
  entry.insert(entry.end(), iv.begin(), iv.end());

  const auto entries = ParseSeig(entry);
  ASSERT_EQ(entries.size(), 1u);
  ASSERT_EQ(entries[0].keySets.size(), 1u);
  EXPECT_EQ(entries[0].keySets[0].perSampleIvSize, 0);
  EXPECT_EQ(entries[0].keySets[0].constantIvSize, 16);
  EXPECT_EQ(entries[0].keySets[0].constantIv, iv);
}

TEST_F(UtilsTest, SeigEntryUnprotectedHasNoConstantIv)
{
  // isProtected 0 with Per_Sample_IV_Size 0: no constant_IV follows the KID
  std::vector<uint8_t> entry = {0x00, 0x00, 0x00, 0x00};
  const std::vector<uint8_t> kid = MakeKid(0x00);
  entry.insert(entry.end(), kid.begin(), kid.end());

  const auto entries = ParseSeig(entry);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].isProtected, 0);
  ASSERT_EQ(entries[0].keySets.size(), 1u);
  EXPECT_EQ(entries[0].keySets[0].constantIvSize, 0);
  EXPECT_TRUE(entries[0].keySets[0].constantIv.empty());
}

TEST_F(UtilsTest, SeigEntryMultiKey)
{
  std::vector<uint8_t> entry = {0x80, 0x00, 0x01, 0x00, 0x02};
  const std::vector<uint8_t> kid1 = MakeKid(0x44);
  const std::vector<uint8_t> kid2 = MakeKid(0x55);
  const std::vector<uint8_t> iv(8, 0x66);
  entry.push_back(0x08);
  entry.insert(entry.end(), kid1.begin(), kid1.end());
  entry.push_back(0x00);
  entry.insert(entry.end(), kid2.begin(), kid2.end());
  entry.push_back(8);
  entry.insert(entry.end(), iv.begin(), iv.end());

  const auto entries = ParseSeig(entry);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_TRUE(entries[0].multiKeyFlag);
  ASSERT_EQ(entries[0].keySets.size(), 2u);
  EXPECT_EQ(entries[0].keySets[0].kid, kid1);
  EXPECT_EQ(entries[0].keySets[0].perSampleIvSize, 8);
  EXPECT_EQ(entries[0].keySets[1].kid, kid2);
  EXPECT_EQ(entries[0].keySets[1].constantIv, iv);
}

TEST_F(UtilsTest, SeigEntryTruncatedKid)
{
  // The KID is cut short: the entry is kept (SGPD index), but without key sets
  const std::vector<uint8_t> entry = {0x00, 0x00, 0x01, 0x08, 1, 2, 3, 4, 5, 6, 7, 8};

  const auto entries = ParseSeig(entry);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_TRUE(entries[0].keySets.empty());
}

TEST_F(UtilsTest, SeigEntryTruncatedHeader)
{
  // The header is cut short: the entry is also kept, unprotected and without key sets
  const std::vector<uint8_t> entry = {0x00, 0x00};

  const auto entries = ParseSeig(entry);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].isProtected, 0);
  EXPECT_TRUE(entries[0].keySets.empty());
}

TEST_F(UtilsTest, FindMoofPsshData)
{
  const std::vector<uint8_t> wvData = {0x12, 0x10, 1,  2,  3,  4,  5,  6,  7,
                                       8,    9,    10, 11, 12, 13, 14, 15, 16};
  const std::vector<uint8_t> commonData = {0xAA, 0xBB};

  // PSSH as a direct child of the MOOF, after a PSSH of another DRM system
  {
    AP4_ContainerAtom moof(AP4_ATOM_TYPE_MOOF);
    moof.AddChild(MakePssh(COMMON_SYSTEM_ID, commonData));
    moof.AddChild(MakePssh(WIDEVINE_SYSTEM_ID, wvData));
    EXPECT_EQ(UTILS::BENTO4::FindMoofPsshData(&moof, WIDEVINE_SYSTEM_ID), wvData);
    EXPECT_EQ(UTILS::BENTO4::FindMoofPsshData(&moof, COMMON_SYSTEM_ID), commonData);
  }
  // PSSH inside the TRAF
  {
    AP4_ContainerAtom moof(AP4_ATOM_TYPE_MOOF);
    AP4_ContainerAtom* traf = new AP4_ContainerAtom(AP4_ATOM_TYPE_TRAF);
    traf->AddChild(MakePssh(WIDEVINE_SYSTEM_ID, wvData));
    moof.AddChild(traf);
    EXPECT_EQ(UTILS::BENTO4::FindMoofPsshData(&moof, WIDEVINE_SYSTEM_ID), wvData);
  }
  // No PSSH of the requested system
  {
    AP4_ContainerAtom moof(AP4_ATOM_TYPE_MOOF);
    moof.AddChild(MakePssh(COMMON_SYSTEM_ID, commonData));
    EXPECT_TRUE(UTILS::BENTO4::FindMoofPsshData(&moof, WIDEVINE_SYSTEM_ID).empty());
    EXPECT_TRUE(UTILS::BENTO4::FindMoofPsshData(nullptr, WIDEVINE_SYSTEM_ID).empty());
  }
}

TEST_F(UtilsTest, ParseWidevinePssh)
{
  const std::vector<uint8_t> kid1 = MakeKid(0x01);
  const std::vector<uint8_t> kid2 = MakeKid(0x02);
  std::vector<std::vector<uint8_t>> keyIds;

  // Round trip with the PSSH generator, two key_id fields
  DRM::ParseWidevinePssh(DRM::MakeWidevinePsshData({kid1, kid2}, {'c', 'i', 'd'}), keyIds);
  ASSERT_EQ(keyIds.size(), 2u);
  EXPECT_EQ(keyIds[0], kid1);
  EXPECT_EQ(keyIds[1], kid2);

  // Fields of other wire types before the key_id are skipped:
  // algorithm (1, varint), a 10 byte varint, a fixed32 and a fixed64 field
  std::vector<uint8_t> data = {0x08, 0x01, 0x18, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                               0xFF, 0xFF, 0x01, 0x25, 1,    2,    3,    4,    0x29, 1,
                               2,    3,    4,    5,    6,    7,    8,    0x12, 0x10};
  data.insert(data.end(), kid1.begin(), kid1.end());
  DRM::ParseWidevinePssh(data, keyIds);
  ASSERT_EQ(keyIds.size(), 1u);
  EXPECT_EQ(keyIds[0], kid1);
}

TEST_F(UtilsTest, ParseWidevinePsshMalformed)
{
  const std::vector<uint8_t> kid1 = MakeKid(0x01);
  std::vector<std::vector<uint8_t>> keyIds;

  // key_id length beyond the end of the data, the valid key id before it is kept
  std::vector<uint8_t> data = {0x12, 0x10};
  data.insert(data.end(), kid1.begin(), kid1.end());
  data.insert(data.end(), {0x12, 0x10, 0xAA, 0xBB});
  DRM::ParseWidevinePssh(data, keyIds);
  ASSERT_EQ(keyIds.size(), 1u);
  EXPECT_EQ(keyIds[0], kid1);

  // Truncated varint length
  DRM::ParseWidevinePssh({0x12, 0x80}, keyIds);
  EXPECT_TRUE(keyIds.empty());

  // Huge length that would overflow a 32 bit int
  DRM::ParseWidevinePssh({0x12, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x00}, keyIds);
  EXPECT_TRUE(keyIds.empty());

  // Varint longer than 10 bytes
  DRM::ParseWidevinePssh({0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01},
                         keyIds);
  EXPECT_TRUE(keyIds.empty());

  // Truncated fixed64 field
  DRM::ParseWidevinePssh({0x29, 1, 2, 3}, keyIds);
  EXPECT_TRUE(keyIds.empty());

  // Empty data
  DRM::ParseWidevinePssh({}, keyIds);
  EXPECT_TRUE(keyIds.empty());
}
