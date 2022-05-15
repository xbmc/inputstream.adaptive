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
    tree = new adaptive::HLSTree(kodiProps, new AESDecrypter(std::string()));
    tree->supportedKeySystem_ = "urn:uuid:EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED";
  }

  void TearDown() override
  {
    testHelper::effectiveUrl.clear();
    delete tree;
    tree = nullptr;
  }

  void OpenTestFileMaster(std::string testfilename, std::string url, std::string manifestHeaders)
  {
    if (url.empty())
      url = "http://foo.bar/" + testfilename;

    SetFileName(testHelper::testFile, testfilename);
    if (!tree->open(url, manifestHeaders))
    {
      printf("open() failed");
      exit(1);
    }
  }

  adaptive::HLSTree::PREPARE_RESULT OpenTestFileVariant(std::string testfilename,
                                    std::string url,
                                    adaptive::AdaptiveTree::Period* per,
                                    adaptive::AdaptiveTree::AdaptationSet* adp,
                                    adaptive::AdaptiveTree::Representation* rep)
  {
    if (!url.empty())
      rep->source_url_ = url;
    SetFileName(testHelper::testFile, testfilename);
    return tree->prepareRepresentation(per, adp, rep);
  }
  adaptive::HLSTree* tree;
};


TEST_F(HLSTreeTest, CalculateSourceUrl)
{
  OpenTestFileMaster("hls/1a2v_master.m3u8", "https://foo.bar/master.m3u8?param=foo", "");
  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://foo.bar/stream_2/out.m3u8",
      tree->current_period_, tree->current_adaptationset_, tree->current_representation_);

  std::string rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);
  EXPECT_EQ(rep_url, "https://foo.bar/stream_2/out.m3u8");
}

TEST_F(HLSTreeTest, CalculateSourceUrlFromRedirectedMasterRelativeUri)
{
  testHelper::effectiveUrl = "https://foo.bar/master.m3u8";

  OpenTestFileMaster("hls/1a2v_master.m3u8", "https://baz.qux/master.m3u8", "");

  std::string rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);

  EXPECT_EQ(rep_url, "https://foo.bar/stream_2/out.m3u8");

  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://foo.bar/stream_2/out.m3u8", tree->current_period_,
      tree->current_adaptationset_, tree->current_representation_);

  rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);
  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(rep_url, "https://foo.bar/stream_2/out.m3u8");
}

TEST_F(HLSTreeTest, CalculateSourceUrlFromRedirectedVariantAbsoluteUri)
{
  OpenTestFileMaster("hls/redirect_absolute_1v_master.m3u8", "https://baz.qux/master.m3u8", "");

  std::string rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);

  EXPECT_EQ(rep_url, "https://bit.ly/abcd");

  testHelper::effectiveUrl = "https://foo.bar/stream_2/out.m3u8";

  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://bit.ly/abcd",
      tree->current_period_, tree->current_adaptationset_, tree->current_representation_);

  rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);

  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(rep_url, "https://bit.ly/abcd");
}

TEST_F(HLSTreeTest, CalculateSourceUrlFromRedirectedMasterAndRedirectedVariantAbsoluteUri)
{
  testHelper::effectiveUrl = "https://baz.qux/master.m3u8";

  OpenTestFileMaster("hls/redirect_absolute_1v_master.m3u8", "https://link.to/1234", "");

  std::string rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);

  EXPECT_EQ(rep_url, "https://bit.ly/abcd");

  testHelper::effectiveUrl = "https://foo.bar/stream_2/out.m3u8";

  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://bit.ly/abcd", tree->current_period_,
      tree->current_adaptationset_, tree->current_representation_);

  rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);
  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(rep_url, "https://bit.ly/abcd");
}

TEST_F(HLSTreeTest,
       CalculateSourceUrlFromRedirectedMasterAndRedirectedVariantAbsoluteUriSameDomains)
{
  testHelper::effectiveUrl = "https://baz.qux/master.m3u8";

  OpenTestFileMaster("hls/redirect_absolute_1v_master.m3u8", "https://bit.ly/1234", "");

  std::string rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);

  EXPECT_EQ(rep_url, "https://bit.ly/abcd");

  testHelper::effectiveUrl = "https://foo.bar/stream_2/out.m3u8";

  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://bit.ly/abcd", tree->current_period_,
      tree->current_adaptationset_, tree->current_representation_);

  rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);
  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(rep_url, "https://bit.ly/abcd");
}

TEST_F(HLSTreeTest, OpenVariant)
{
  OpenTestFileMaster("hls/1a2v_master.m3u8", "https://foo.bar/master.m3u8", "");

  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://foo.bar/stream_2.m3u8", tree->current_period_,
      tree->current_adaptationset_, tree->current_representation_);

  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(tree->base_url_, "https://foo.bar/");
}

TEST_F(HLSTreeTest, ParseKeyUriStartingWithSlash)
{
  OpenTestFileMaster("hls/1v_master.m3u8",
                     "https://foo.bar/hls/video/stream_name/master.m3u8", "");

  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/ts_aes_keyuriwithslash_stream_0.m3u8",
      "https://foo.bar/hls/video/stream_name/chunklist.m3u8", tree->current_period_,
      tree->current_adaptationset_, tree->current_representation_);

  std::string pssh_url = tree->BuildDownloadUrl(tree->current_period_->psshSets_[1].pssh_);
  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(pssh_url,
            "https://foo.bar/hls/key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, ParseKeyUriStartingWithSlashFromRedirect)
{
  testHelper::effectiveUrl = "https://foo.bar/hls/video/stream_name/master.m3u8";

  OpenTestFileMaster("hls/1v_master.m3u8", "https://baz.qux/hls/video/stream_name/master.m3u8",
                     "");

  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/ts_aes_keyuriwithslash_stream_0.m3u8",
      "https://foo.bar/hls/video/stream_name/chunklist.m3u8", tree->current_period_,
      tree->current_adaptationset_, tree->current_representation_);

  std::string pssh_url = tree->BuildDownloadUrl(tree->current_period_->psshSets_[1].pssh_);
  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(pssh_url,
            "https://foo.bar/hls/key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, ParseKeyUriAbsolute)
{
  OpenTestFileMaster("hls/1v_master.m3u8",
                     "https://foo.bar/hls/video/stream_name/master.m3u8", "");

  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/ts_aes_keyuriabsolute_stream_0.m3u8",
      "https://foo.bar/hls/video/stream_name/chunklist.m3u8", tree->current_period_,
      tree->current_adaptationset_, tree->current_representation_);

  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(tree->current_period_->psshSets_[1].pssh_,
            "https://foo.bar/hls/key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, ParseKeyUriRelative)
{
  OpenTestFileMaster("hls/1v_master.m3u8", "https://foo.bar/hls/video/stream_name/master.m3u8",
                     "");

  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/ts_aes_keyurirelative_stream_0.m3u8",
      "https://foo.bar/hls/video/stream_name/chunklist.m3u8", tree->current_period_,
      tree->current_adaptationset_, tree->current_representation_);

  std::string pssh_url = tree->BuildDownloadUrl(tree->current_period_->psshSets_[1].pssh_);
  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(pssh_url,
            "https://foo.bar/hls/video/stream_name/../../key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, ParseKeyUriRelativeFromRedirect)
{
  testHelper::effectiveUrl = "https://foo.bar/hls/video/stream_name/master.m3u8";

  OpenTestFileMaster("hls/1v_master.m3u8",
                     "https://baz.qux/hls/video/stream_name/master.m3u8", "");
  std::string var_download_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]
          ->representations_[0]
          ->source_url_); // https://baz.qux/hls/video/stream_name/ts_aes_uriwithslash_chunklist.m3u8
  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/ts_aes_keyurirelative_stream_0.m3u8",
      var_download_url,
      tree->current_period_,
      tree->current_adaptationset_,
      tree->current_representation_);

  std::string pssh_url = tree->BuildDownloadUrl(tree->current_period_->psshSets_[1].pssh_);
  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(pssh_url,
            "https://foo.bar/hls/video/stream_name/../../key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, PtsSetInMultiPeriod)
{
  OpenTestFileMaster("hls/1a2v_master.m3u8", "https://foo.bar/master.m3u8", "");
  std::string var_download_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[1]->source_url_);

  adaptive::HLSTree::PREPARE_RESULT res =
      OpenTestFileVariant("hls/disco_fmp4_noenc_v_stream_1.m3u8", var_download_url,
                          tree->periods_[0], tree->periods_[0]->adaptationSets_[0],
                          tree->periods_[0]->adaptationSets_[0]->representations_[1]);

  uint64_t pts =
      tree->periods_[1]->adaptationSets_[0]->representations_[1]->segments_.data[0].startPTS_;
  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(pts, 21000000);

  var_download_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[1]->representations_[0]->source_url_);

  res = OpenTestFileVariant("hls/disco_fmp4_noenc_a_stream_0.m3u8", var_download_url,
                            tree->periods_[1], tree->periods_[1]->adaptationSets_[1],
                            tree->periods_[1]->adaptationSets_[1]->representations_[0]);

  pts = tree->periods_[1]->adaptationSets_[1]->representations_[0]->segments_.data[0].startPTS_;
  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(pts, 20993000);
}
