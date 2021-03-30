#include "TestHelper.h"
#include <gtest/gtest.h>


class HLSTreeTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    tree = new adaptive::HLSTree(new AESDecrypter(std::string()));
    tree->supportedKeySystem_ = "urn:uuid:EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED";
  }

  void TearDown() override
  {
    effectiveUrl.clear();
    delete tree;
    tree = nullptr;
  }

  void OpenTestFileMaster(std::string testfilename, std::string url, std::string manifestHeaders)
  {
    SetFileName(testFile, testfilename);
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
    SetFileName(testFile, testfilename);
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
  EXPECT_EQ(tree->base_url_, "https://foo.bar/");
  EXPECT_EQ(rep_url, "https://foo.bar/stream_2/out.m3u8");
}


TEST_F(HLSTreeTest, CalculateSourceUrlFromRedirectedMasterRelativeUri)
{
  effectiveUrl = "https://foo.bar/master.m3u8";

  OpenTestFileMaster("hls/1a2v_master.m3u8", "https://baz.qux/master.m3u8", "");
  
  std::string rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);

  EXPECT_EQ(rep_url, "https://foo.bar/stream_2/out.m3u8");
  
  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://foo.bar/stream_2/out.m3u8", tree->current_period_,
      tree->current_adaptationset_, tree->current_representation_);
  
  rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);
  // base_url_ should never change after opening stream regardless of redirects
  EXPECT_EQ(tree->base_url_, "https://baz.qux/");
  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(rep_url, "https://foo.bar/stream_2/out.m3u8");
}


TEST_F(HLSTreeTest, CalculateSourceUrlFromRedirectedVariantAbsoluteUri)
{
  OpenTestFileMaster("hls/redirect_absolute_1v_master.m3u8", "https://baz.qux/master.m3u8", "");

  std::string rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);
  
  EXPECT_EQ(rep_url, "https://bit.ly/abcd");

  effectiveUrl = "https://foo.bar/stream_2/out.m3u8";

  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://bit.ly/abcd",
      tree->current_period_, tree->current_adaptationset_, tree->current_representation_);

  rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);
  EXPECT_EQ(tree->base_url_, "https://baz.qux/");
  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(rep_url, "https://bit.ly/abcd");
}


TEST_F(HLSTreeTest, CalculateSourceUrlFromRedirectedMasterAndRedirectedVariantAbsoluteUri)
{
  effectiveUrl = "https://baz.qux/master.m3u8";

  OpenTestFileMaster("hls/redirect_absolute_1v_master.m3u8", "https://link.to/1234", "");

  std::string rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);
  
  EXPECT_EQ(rep_url, "https://bit.ly/abcd");

  effectiveUrl = "https://foo.bar/stream_2/out.m3u8";

  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://bit.ly/abcd", tree->current_period_,
      tree->current_adaptationset_, tree->current_representation_);

  rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);
  EXPECT_EQ(tree->base_url_, "https://link.to/");
  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(rep_url, "https://bit.ly/abcd");
}


TEST_F(HLSTreeTest,
       CalculateSourceUrlFromRedirectedMasterAndRedirectedVariantAbsoluteUriSameDomains)
{
  GTEST_SKIP();
  effectiveUrl = "https://baz.qux/master.m3u8";

  OpenTestFileMaster("hls/redirect_absolute_1v_master.m3u8", "https://bit.ly/1234", "");

  std::string rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);
  
  EXPECT_EQ(rep_url, "https://bit.ly/abcd");

  effectiveUrl = "https://foo.bar/stream_2/out.m3u8";

  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://bit.ly/abcd", tree->current_period_,
      tree->current_adaptationset_, tree->current_representation_);

  rep_url = tree->BuildDownloadUrl(
      tree->current_period_->adaptationSets_[0]->representations_[0]->source_url_);
  EXPECT_EQ(tree->base_url_, "https://bit.ly/");
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
  EXPECT_EQ(tree->base_url_, "https://foo.bar/hls/video/stream_name/");
  EXPECT_EQ(pssh_url,
            "https://foo.bar/hls/key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, ParseKeyUriStartingWithSlashFromRedirect)
{
  effectiveUrl = "https://foo.bar/hls/video/stream_name/master.m3u8";

  OpenTestFileMaster("hls/1v_master.m3u8", "https://baz.qux/hls/video/stream_name/master.m3u8",
                     "");

  adaptive::HLSTree::PREPARE_RESULT res = OpenTestFileVariant(
      "hls/ts_aes_keyuriwithslash_stream_0.m3u8",
      "https://foo.bar/hls/video/stream_name/chunklist.m3u8", tree->current_period_,
      tree->current_adaptationset_, tree->current_representation_);

  std::string pssh_url = tree->BuildDownloadUrl(tree->current_period_->psshSets_[1].pssh_);
  EXPECT_EQ(res, adaptive::HLSTree::PREPARE_RESULT_OK);
  EXPECT_EQ(tree->base_url_, "https://baz.qux/hls/video/stream_name/");
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
  EXPECT_EQ(tree->base_url_, "https://foo.bar/hls/video/stream_name/");
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
  EXPECT_EQ(tree->base_url_, "https://foo.bar/hls/video/stream_name/");
  EXPECT_EQ(pssh_url,
            "https://foo.bar/hls/video/stream_name/../../key/key.php?stream=stream_name");
}


TEST_F(HLSTreeTest, ParseKeyUriRelativeFromRedirect)
{
  effectiveUrl = "https://foo.bar/hls/video/stream_name/master.m3u8";

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
  EXPECT_EQ(tree->base_url_, "https://baz.qux/hls/video/stream_name/");
  EXPECT_EQ(pssh_url,
            "https://foo.bar/hls/video/stream_name/../../key/key.php?stream=stream_name");
}
