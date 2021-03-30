#include "TestHelper.h"

#include <gtest/gtest.h>


class DASHTreeTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    tree = new DASHTestTree;
    tree->supportedKeySystem_ = "urn:uuid:EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED";
  }

  void TearDown() override
  { 
    effectiveUrl.clear();
    delete tree;
    tree = nullptr;
  }

  void OpenTestFile(std::string testfilename, std::string url, std::string manifestHeaders)
  { 
    SetFileName(testFile, testfilename);
    if (!tree->open(url, manifestHeaders))
    {
      printf("open() failed");
      exit(1);
    }
  }

  DASHTestTree* tree;
};


TEST_F(DASHTreeTest, CalculateBaseURL)
{
  // No BaseURL tags
  OpenTestFile("mpd/segtpl.mpd", "https://foo.bar/mpd/test.mpd", "");
  EXPECT_EQ(tree->base_url_, "https://foo.bar/mpd/");
  }

TEST_F(DASHTreeTest, CalculateBaseDomain)
  {
    OpenTestFile("mpd/segtpl.mpd", "https://foo.bar/mpd/test.mpd", "");

    EXPECT_EQ(tree->base_domain_, "https://foo.bar");
  }

TEST_F(DASHTreeTest, CalculateEffectiveUrlFromRedirect)
  {
    // like base_url_, effective_url_ should be path, not including filename
    effectiveUrl = "https://foo.bar/mpd/stream.mpd";
    OpenTestFile("mpd/segtpl.mpd", "https://bit.ly/abcd", "");
    EXPECT_EQ(tree->effective_url_, "https://foo.bar/mpd/");
  }

TEST_F(DASHTreeTest, CalculateBaseURLFromBaseURLTag)
  {
    OpenTestFile("mpd/segtpl_baseurlinmpd.mpd", "https://bit.ly/abcd", "");
    EXPECT_EQ(tree->current_period_->base_url_, "https://foo.bar/mpd/");
  }

TEST_F(DASHTreeTest, CalculateSegTplWithNoSlashs)
{
  // BaseURL inside period with no trailing slash, uses segtpl, media/init doesn't start with slash
  OpenTestFile("mpd/segtpl_baseurl_noslashs.mpd", "https://foo.bar/initialpath/test.mpd", "");

  adaptive::AdaptiveTree::SegmentTemplate segtpl =
      tree->periods_[0]->adaptationSets_[0]->representations_[0]->segtpl_;
  
  EXPECT_EQ(segtpl.initialization, "https://foo.bar/guid.ism/dash/media-video=66000.dash");
  EXPECT_EQ(segtpl.media, "https://foo.bar/guid.ism/dash/media-video=66000-$Number$.m4s");
}

TEST_F(DASHTreeTest, CalculateSegTplWithMediaInitSlash)
{
  // BaseURL inside period with no trailing slash, uses segtpl, media/init starts with slash
  OpenTestFile("mpd/segtpl_slash_baseurl_noslash.mpd", "https://foo.bar/initialpath/test.mpd", "");

  adaptive::AdaptiveTree::SegmentTemplate segtpl =
      tree->periods_[0]->adaptationSets_[0]->representations_[0]->segtpl_;

  EXPECT_EQ(segtpl.initialization, "https://foo.bar/media-video=66000.dash");
  EXPECT_EQ(segtpl.media, "https://foo.bar/media-video=66000-$Number$.m4s");
}

TEST_F(DASHTreeTest, CalculateSegTplWithBaseURLSlash)
{
  // BaseURL inside period with trailing slash, uses segtpl, media/init doesn't start with slash
  OpenTestFile("mpd/segtpl_noslash_baseurl_slash.mpd", "https://foo.bar/initialpath/test.mpd", "");

  adaptive::AdaptiveTree::SegmentTemplate segtpl =
      tree->periods_[0]->adaptationSets_[0]->representations_[0]->segtpl_;

  EXPECT_EQ(segtpl.initialization, "https://foo.bar/guid.ism/dash/media-video=66000.dash");
  EXPECT_EQ(segtpl.media, "https://foo.bar/guid.ism/dash/media-video=66000-$Number$.m4s");
}

TEST_F(DASHTreeTest, CalculateSegTplWithBaseURLAndMediaInitSlash)
{
  // BaseURL inside period with trailing slash, uses segtpl, media/init starts with slash
  OpenTestFile("mpd/segtpl_slash_baseurl_slash.mpd", "https://foo.bar/initialpath/test.mpd", "");

  adaptive::AdaptiveTree::SegmentTemplate segtpl =
      tree->periods_[0]->adaptationSets_[0]->representations_[0]->segtpl_;

  EXPECT_EQ(segtpl.initialization, "https://foo.bar/media-video=66000.dash");
  EXPECT_EQ(segtpl.media, "https://foo.bar/media-video=66000-$Number$.m4s");
}

TEST_F(DASHTreeTest, CalculateBaseURLInRepRangeBytes)
{
  // Byteranged indexing
  OpenTestFile("mpd/segmentbase.mpd", "https://foo.bar/test.mpd", "");
  EXPECT_EQ(tree->periods_[0]->adaptationSets_[0]->representations_[0]->url_,
            "https://foo.bar/video/23.98p/r0/vid10.mp4");
}

TEST_F(DASHTreeTest, CalculateCorrectSegmentNumbersFromSegmentTimeline)
{
  // SegmentTimeline, availabilityStartTime is greater than epoch
  OpenTestFile("mpd/segtimeline_live_ast.mpd", "", "");

  adaptive::SPINCACHE<adaptive::AdaptiveTree::Segment> segments =
      tree->periods_[0]->adaptationSets_[0]->representations_[0]->segments_;

  EXPECT_EQ(segments.size(), 13);
  EXPECT_EQ(segments[0]->range_end_, 487050);
  EXPECT_EQ(segments[12]->range_end_, 487062);
}
