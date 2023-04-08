/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TestHelper.h"

#include "../utils/PropertiesUtils.h"

#include <gtest/gtest.h>


class HLSTreeTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    UTILS::PROPERTIES::KodiProperties kodiProps;

    m_reprChooser = new CTestRepresentationChooserDefault();
    m_reprChooser->Initialize(kodiProps.m_chooserProps);

    tree = new HLSTestTree(m_reprChooser);
    tree->Configure(kodiProps);
    tree->m_supportedKeySystem = "urn:uuid:EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED";
  }

  void TearDown() override
  {
    testHelper::effectiveUrl.clear();
    delete tree;
    tree = nullptr;
    delete m_reprChooser;
    m_reprChooser = nullptr;
  }

  void OpenTestFileMaster(std::string filePath)
  {
    OpenTestFileMaster(filePath, "http://foo.bar/" + filePath);
  }

  void OpenTestFileMaster(std::string filePath, std::string url)
  {
    OpenTestFileMaster(filePath, url, {});
  }

  void OpenTestFileMaster(std::string filePath,
                          std::string url,
                          std::map<std::string, std::string> manifestHeaders)
  {
    SetFileName(testHelper::testFile, filePath);

    tree->SetManifestUpdateParam(url, "");
    if (!tree->open(url, manifestHeaders))
    {
      LOG::Log(LOGERROR, "Cannot open \"%s\" HLS manifest.", url.c_str());
      exit(1);
    }

    tree->m_currentAdpSet = tree->m_periods[0]->GetAdaptationSets()[0].get();
    tree->m_currentRepr = tree->m_currentAdpSet->GetRepresentations()[0].get();
  }

  PLAYLIST::PrepareRepStatus OpenTestFileVariant(std::string filePath,
                                                 std::string url,
                                                 PLAYLIST::CPeriod* per,
                                                 PLAYLIST::CAdaptationSet* adp,
                                                 PLAYLIST::CRepresentation* rep)
  {
    if (!url.empty())
      rep->SetSourceUrl(url);

    SetFileName(testHelper::testFile, filePath);
    return tree->prepareRepresentation(per, adp, rep);
  }

  adaptive::CHLSTree* tree;
  CHOOSER::IRepresentationChooser* m_reprChooser{nullptr};
};


TEST_F(HLSTreeTest, CalculateSourceUrl)
{
  OpenTestFileMaster("hls/1a2v_master.m3u8", "https://foo.bar/master.m3u8?param=foo");

  PLAYLIST::PrepareRepStatus res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://foo.bar/stream_2/out.m3u8",
      tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  std::string rep_url = tree->BuildDownloadUrl(
      tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl());
  EXPECT_EQ(rep_url, "https://foo.bar/stream_2/out.m3u8");
}

TEST_F(HLSTreeTest, CalculateSourceUrlFromRedirectedMasterRelativeUri)
{
  testHelper::effectiveUrl = "https://foo.bar/master.m3u8";

  OpenTestFileMaster("hls/1a2v_master.m3u8", "https://baz.qux/master.m3u8");

  std::string rep_url = tree->BuildDownloadUrl(
      tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl());

  EXPECT_EQ(rep_url, "https://foo.bar/stream_2/out.m3u8");

  PLAYLIST::PrepareRepStatus res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://foo.bar/stream_2/out.m3u8", tree->m_currentPeriod,
      tree->m_currentAdpSet, tree->m_currentRepr);

  rep_url = tree->BuildDownloadUrl(
      tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl());
  EXPECT_EQ(res, PLAYLIST::PrepareRepStatus::OK);
  EXPECT_EQ(rep_url, "https://foo.bar/stream_2/out.m3u8");
}

TEST_F(HLSTreeTest, CalculateSourceUrlFromRedirectedVariantAbsoluteUri)
{
  OpenTestFileMaster("hls/redirect_absolute_1v_master.m3u8", "https://baz.qux/master.m3u8");

  std::string rep_url = tree->BuildDownloadUrl(
      tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl());

  EXPECT_EQ(rep_url, "https://bit.ly/abcd");

  testHelper::effectiveUrl = "https://foo.bar/stream_2/out.m3u8";

  PLAYLIST::PrepareRepStatus res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://bit.ly/abcd",
      tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  rep_url = tree->BuildDownloadUrl(
      tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl());

  EXPECT_EQ(res, PLAYLIST::PrepareRepStatus::OK);
  EXPECT_EQ(rep_url, "https://bit.ly/abcd");
}

TEST_F(HLSTreeTest, CalculateSourceUrlFromRedirectedMasterAndRedirectedVariantAbsoluteUri)
{
  testHelper::effectiveUrl = "https://baz.qux/master.m3u8";

  OpenTestFileMaster("hls/redirect_absolute_1v_master.m3u8", "https://link.to/1234");

  std::string rep_url = tree->BuildDownloadUrl(
      tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl());

  EXPECT_EQ(rep_url, "https://bit.ly/abcd");

  testHelper::effectiveUrl = "https://foo.bar/stream_2/out.m3u8";

  PLAYLIST::PrepareRepStatus res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://bit.ly/abcd", tree->m_currentPeriod,
      tree->m_currentAdpSet, tree->m_currentRepr);

  rep_url = tree->BuildDownloadUrl(
      tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl());
  EXPECT_EQ(res, PLAYLIST::PrepareRepStatus::OK);
  EXPECT_EQ(rep_url, "https://bit.ly/abcd");
}

TEST_F(HLSTreeTest,
       CalculateSourceUrlFromRedirectedMasterAndRedirectedVariantAbsoluteUriSameDomains)
{
  testHelper::effectiveUrl = "https://baz.qux/master.m3u8";

  OpenTestFileMaster("hls/redirect_absolute_1v_master.m3u8", "https://bit.ly/1234");

  std::string rep_url = tree->BuildDownloadUrl(
      tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl());

  EXPECT_EQ(rep_url, "https://bit.ly/abcd");

  testHelper::effectiveUrl = "https://foo.bar/stream_2/out.m3u8";

  PLAYLIST::PrepareRepStatus res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://bit.ly/abcd", tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  rep_url = tree->BuildDownloadUrl(
      tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl());
  EXPECT_EQ(res, PLAYLIST::PrepareRepStatus::OK);
  EXPECT_EQ(rep_url, "https://bit.ly/abcd");
}

TEST_F(HLSTreeTest, OpenVariant)
{
  OpenTestFileMaster("hls/1a2v_master.m3u8", "https://foo.bar/master.m3u8");

  PLAYLIST::PrepareRepStatus res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://foo.bar/stream_2.m3u8", tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(res, PLAYLIST::PrepareRepStatus::OK);
  EXPECT_EQ(tree->base_url_, "https://foo.bar/");
}

TEST_F(HLSTreeTest, ParseKeyUriStartingWithSlash)
{
  OpenTestFileMaster("hls/1v_master.m3u8", "https://foo.bar/hls/video/stream_name/master.m3u8");

  PLAYLIST::PrepareRepStatus res = OpenTestFileVariant(
      "hls/ts_aes_keyuriwithslash_stream_0.m3u8",
      "https://foo.bar/hls/video/stream_name/chunklist.m3u8", tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  std::string pssh_url = tree->BuildDownloadUrl(tree->m_currentPeriod->GetPSSHSets()[1].pssh_);
  EXPECT_EQ(res, PLAYLIST::PrepareRepStatus::OK);
  EXPECT_EQ(pssh_url,
            "https://foo.bar/hls/key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, ParseKeyUriStartingWithSlashFromRedirect)
{
  testHelper::effectiveUrl = "https://foo.bar/hls/video/stream_name/master.m3u8";

  OpenTestFileMaster("hls/1v_master.m3u8", "https://baz.qux/hls/video/stream_name/master.m3u8");

  PLAYLIST::PrepareRepStatus res = OpenTestFileVariant(
      "hls/ts_aes_keyuriwithslash_stream_0.m3u8",
      "https://foo.bar/hls/video/stream_name/chunklist.m3u8", tree->m_currentPeriod,
      tree->m_currentAdpSet, tree->m_currentRepr);

  std::string pssh_url = tree->BuildDownloadUrl(tree->m_currentPeriod->GetPSSHSets()[1].pssh_);
  EXPECT_EQ(res, PLAYLIST::PrepareRepStatus::OK);
  EXPECT_EQ(pssh_url,
            "https://foo.bar/hls/key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, ParseKeyUriAbsolute)
{
  OpenTestFileMaster("hls/1v_master.m3u8", "https://foo.bar/hls/video/stream_name/master.m3u8");

  PLAYLIST::PrepareRepStatus res = OpenTestFileVariant(
      "hls/ts_aes_keyuriabsolute_stream_0.m3u8",
      "https://foo.bar/hls/video/stream_name/chunklist.m3u8", tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(res, PLAYLIST::PrepareRepStatus::OK);
  EXPECT_EQ(tree->m_currentPeriod->GetPSSHSets()[1].pssh_,
            "https://foo.bar/hls/key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, ParseKeyUriRelative)
{
  OpenTestFileMaster("hls/1v_master.m3u8", "https://foo.bar/hls/video/stream_name/master.m3u8");

  PLAYLIST::PrepareRepStatus res = OpenTestFileVariant(
      "hls/ts_aes_keyurirelative_stream_0.m3u8",
      "https://foo.bar/hls/video/stream_name/chunklist.m3u8", tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  std::string pssh_url = tree->BuildDownloadUrl(tree->m_currentPeriod->GetPSSHSets()[1].pssh_);
  EXPECT_EQ(res, PLAYLIST::PrepareRepStatus::OK);
  EXPECT_EQ(pssh_url, "https://foo.bar/hls/key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, ParseKeyUriRelativeFromRedirect)
{
  testHelper::effectiveUrl = "https://foo.bar/hls/video/stream_name/master.m3u8";

  OpenTestFileMaster("hls/1v_master.m3u8", "https://baz.qux/hls/video/stream_name/master.m3u8");
  std::string var_download_url = tree->BuildDownloadUrl(
      tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl()); // https://baz.qux/hls/video/stream_name/ts_aes_uriwithslash_chunklist.m3u8

  PLAYLIST::PrepareRepStatus res = OpenTestFileVariant(
      "hls/ts_aes_keyurirelative_stream_0.m3u8",
      var_download_url,
      tree->m_currentPeriod,
      tree->m_currentAdpSet, tree->m_currentRepr);

  std::string pssh_url = tree->BuildDownloadUrl(tree->m_currentPeriod->GetPSSHSets()[1].pssh_);
  EXPECT_EQ(res, PLAYLIST::PrepareRepStatus::OK);
  EXPECT_EQ(pssh_url, "https://foo.bar/hls/key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, PtsSetInMultiPeriod)
{
  OpenTestFileMaster("hls/1a2v_master.m3u8", "https://foo.bar/master.m3u8");
  std::string var_download_url = tree->BuildDownloadUrl(
      tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[1]->GetSourceUrl());

  auto& periodFirst = tree->m_periods[0];

  PLAYLIST::PrepareRepStatus res =
      OpenTestFileVariant("hls/disco_fmp4_noenc_v_stream_1.m3u8", var_download_url,
                          periodFirst.get(), periodFirst->GetAdaptationSets()[0].get(),
                          periodFirst->GetAdaptationSets()[0]->GetRepresentations()[1].get());

  auto& periodSecond = tree->m_periods[1];

  uint64_t pts = periodSecond->GetAdaptationSets()[0]->GetRepresentations()[1]->SegmentTimeline().GetData()[0].startPTS_;
  EXPECT_EQ(res, PLAYLIST::PrepareRepStatus::OK);
  EXPECT_EQ(pts, 0);

  var_download_url = tree->BuildDownloadUrl(
      tree->m_currentPeriod->GetAdaptationSets()[1]->GetRepresentations()[0]->GetSourceUrl());

  res = OpenTestFileVariant("hls/disco_fmp4_noenc_a_stream_0.m3u8", var_download_url,
                            periodSecond.get(), periodSecond->GetAdaptationSets()[1].get(),
                            periodSecond->GetAdaptationSets()[1]->GetRepresentations()[0].get());

  pts = periodSecond->GetAdaptationSets()[1]->GetRepresentations()[0]->SegmentTimeline().GetData()[0].startPTS_;

  EXPECT_EQ(res, PLAYLIST::PrepareRepStatus::OK);
  EXPECT_EQ(pts, 0);
}
