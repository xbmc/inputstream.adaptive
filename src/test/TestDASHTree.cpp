/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TestHelper.h"

#include "../utils/PropertiesUtils.h"
#include "../utils/UrlUtils.h"

#include <gtest/gtest.h>

class DASHTreeTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_reprChooser = new CTestRepresentationChooserDefault();
    tree = new DASHTestTree();
  }

  void TearDown() override
  {
    testHelper::effectiveUrl.clear();
    delete tree;
    tree = nullptr;
    delete m_reprChooser;
    m_reprChooser = nullptr;
  }

  void OpenTestFile(std::string filePath) { OpenTestFile(filePath, "http://foo.bar/" + filePath); }

  void OpenTestFile(std::string filePath, std::string url) { OpenTestFile(filePath, url, {}, ""); }

  void OpenTestFile(std::string filePath, std::string url, std::map<std::string, std::string> manifestHeaders)
  {
    OpenTestFile(filePath, url, manifestHeaders, "");
  }

  void OpenTestFile(std::string filePath,
                    std::string url,
                    std::map<std::string, std::string> manifestHeaders,
                    std::string manifestUpdParams)
  {
    testHelper::testFile = filePath;

    // Download the manifest
    UTILS::CURL::HTTPResponse resp;
    if (!testHelper::DownloadFile(url, {}, {}, resp))
    {
      LOG::Log(LOGERROR, "Cannot download \"%s\" DASH manifest file.", url.c_str());
      exit(1);
    }

    m_reprChooser->Initialize(m_kodiProps.m_chooserProps);
    // We set the download speed to calculate the initial network bandwidth
    m_reprChooser->SetDownloadSpeed(500000);

    tree->Configure(m_kodiProps, m_reprChooser, "urn:uuid:EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED",
                    manifestUpdParams);

    // Parse the manifest
    if (!tree->Open(resp.effectiveUrl, resp.headers, resp.data))
    {
      LOG::Log(LOGERROR, "Cannot open \"%s\" DASH manifest.", url.c_str());
      exit(1);
    }
  }

  DASHTestTree* tree;
  CHOOSER::IRepresentationChooser* m_reprChooser{nullptr};
  UTILS::PROPERTIES::KodiProperties m_kodiProps;
};

class DASHTreeAdaptiveStreamTest : public DASHTreeTest
{
protected:
  void SetUp() override
  {
    DASHTreeTest::SetUp();
  }

  void TearDown() override
  {
    delete testStream;
    DASHTreeTest::TearDown();
  }

  void SetTestStream(TestAdaptiveStream* newStream)
  {
    delete testStream;
    testHelper::downloadList.clear();
    testStream = newStream;
  }

  TestAdaptiveStream* NewStream(PLAYLIST::CAdaptationSet* adp,
                                bool playTimeshiftBuffer = true)
  {
    auto initialRepr{tree->GetRepChooser()->GetRepresentation(adp)};
    UTILS::PROPERTIES::KodiProperties kodiProps;
    kodiProps.m_playTimeshiftBuffer = playTimeshiftBuffer;

    return new TestAdaptiveStream(*tree, adp, initialRepr, kodiProps, false);
  }

  void ReadSegments(TestAdaptiveStream* stream,
                    uint32_t bytesToRead,
                    uint32_t reads)
  {
    // Rudimentary simulation of running a stream and consuming segment data.
    // Normally AdaptiveStream::read is called from a sample reader for the exact
    // amount of bytes needed to supply the next sample until the segment is
    // exhausted. Here our segments are a fixed size (16 bytes) and for testing we can
    // optimally call to read 1 segment per AdaptiveStream::read
    bool ret;
    for (unsigned int i = 0; i < reads; i++)
    {
      ret = stream->read(&buf, bytesToRead);
      // prevent race condition leading to deadlock
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      if (!ret)
        break;
    }

    // Decrement last updated time so live manifest will always refresh on each segment
    // in order to test manifest update changes
    tree->SetLastUpdated(std::chrono::system_clock::now() - std::chrono::seconds(2));
    stream->SetLastUpdated(std::chrono::system_clock::now() - std::chrono::seconds(2));
  }

  TestAdaptiveStream* testStream = nullptr;
  TestAdaptiveStream* newStream = nullptr;
  std::vector<std::string> downloadedUrls;
  std::map<std::string, std::string> mediaHeaders;
  unsigned char buf[16];
};

TEST_F(DASHTreeTest, CalculateBaseURL)
{
  // No BaseURL tags
  OpenTestFile("mpd/segtpl.mpd", "https://foo.bar/mpd/test.mpd");
  EXPECT_EQ(tree->base_url_, "https://foo.bar/mpd/");
}

TEST_F(DASHTreeTest, CalculateBaseUrlFromRedirect)
{
  testHelper::effectiveUrl = "https://foo.bar/mpd/stream.mpd";
  OpenTestFile("mpd/segtpl.mpd", "https://bit.ly/abcd.mpd");
  EXPECT_EQ(tree->base_url_, "https://foo.bar/mpd/");
  EXPECT_EQ(tree->manifest_url_, "https://foo.bar/mpd/stream.mpd");
}

TEST_F(DASHTreeTest, CalculateBaseURLFromBaseURLTag)
{
  OpenTestFile("mpd/segtpl_baseurlinmpd.mpd", "https://bit.ly/abcd");
  EXPECT_EQ(tree->m_currentPeriod->GetBaseUrl(), "https://foo.bar/mpd/");
}

TEST_F(DASHTreeTest, CalculateBaseURLWithNoSlashOutsidePeriod)
{
  // BaseURL outside period with no trailing slash
  OpenTestFile("mpd/segtpl_baseurl_noslash_outside.mpd", "https://bit.ly/abcd");

  auto& segtpl = tree->m_periods[0]->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSegmentTemplate();

  EXPECT_EQ(tree->m_currentPeriod->GetBaseUrl(), "https://foo.bar/mpd/");
  EXPECT_EQ(STR(segtpl->GetInitialization()), "https://foo.bar/mpd/V300/init.mp4");
  EXPECT_EQ(STR(segtpl->GetMediaUrl()), "https://foo.bar/mpd/V300/$Number$.m4s");
}

TEST_F(DASHTreeTest, CalculateSegTplWithNoSlashes)
{
  // BaseURL inside period with no trailing slash, uses segtpl, media/init doesn't start with slash
  OpenTestFile("mpd/segtpl_baseurl_noslashs.mpd", "https://foo.bar/initialpath/test.mpd");

  auto& segtpl = tree->m_periods[0]->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSegmentTemplate();

  EXPECT_EQ(STR(segtpl->GetInitialization()), "https://foo.bar/guid.ism/dash/media-video=66000.dash");
  EXPECT_EQ(STR(segtpl->GetMediaUrl()), "https://foo.bar/guid.ism/dash/media-video=66000-$Number$.m4s");
}

TEST_F(DASHTreeTest, CalculateSegTplWithMediaInitSlash)
{
  // BaseURL inside period with no trailing slash, uses segtpl, media/init starts with slash
  OpenTestFile("mpd/segtpl_slash_baseurl_noslash.mpd", "https://foo.bar/initialpath/test.mpd");

  auto& segtpl = tree->m_periods[0]->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSegmentTemplate();

  EXPECT_EQ(STR(segtpl->GetInitialization()), "https://foo.bar/media-video=66000.dash");
  EXPECT_EQ(STR(segtpl->GetMediaUrl()), "https://foo.bar/media-video=66000-$Number$.m4s");
}

TEST_F(DASHTreeTest, CalculateSegTplWithBaseURLSlash)
{
  // BaseURL inside period with trailing slash, uses segtpl, media/init doesn't start with slash
  OpenTestFile("mpd/segtpl_noslash_baseurl_slash.mpd", "https://foo.bar/initialpath/test.mpd");

  auto& segtpl = tree->m_periods[0]->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSegmentTemplate();

  EXPECT_EQ(STR(segtpl->GetInitialization()), "https://foo.bar/guid.ism/dash/media-video=66000.dash");
  EXPECT_EQ(STR(segtpl->GetMediaUrl()), "https://foo.bar/guid.ism/dash/media-video=66000-$Number$.m4s");
}

TEST_F(DASHTreeTest, CalculateSegTplWithBaseURLAndMediaInitSlash)
{
  // BaseURL inside period with trailing slash, uses segtpl, media/init starts with slash
  OpenTestFile("mpd/segtpl_slash_baseurl_slash.mpd", "https://foo.bar/initialpath/test.mpd");

  auto& segtpl = tree->m_periods[0]->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSegmentTemplate();

  EXPECT_EQ(STR(segtpl->GetInitialization()), "https://foo.bar/media-video=66000.dash");
  EXPECT_EQ(STR(segtpl->GetMediaUrl()), "https://foo.bar/media-video=66000-$Number$.m4s");
}

TEST_F(DASHTreeTest, CalculateBaseURLInRepRangeBytes)
{
  // Byteranged indexing
  OpenTestFile("mpd/segmentbase.mpd", "https://foo.bar/test.mpd");
  EXPECT_EQ(tree->m_periods[0]->GetAdaptationSets()[0]->GetRepresentations()[0]->GetUrl(),
            "https://foo.bar/video/23.98p/r0/vid10.mp4");
}

TEST_F(DASHTreeTest, CalculateCorrectSegmentNumbersFromSegmentTimeline)
{
  // SegmentTimeline, availabilityStartTime is greater than epoch
  OpenTestFile("mpd/segtimeline_live_ast.mpd");

  auto& segments =
      tree->m_periods[0]->GetAdaptationSets()[0]->GetRepresentations()[0]->SegmentTimeline();

  EXPECT_EQ(segments.GetSize(), 13);
  EXPECT_EQ(segments.Get(0)->range_end_, 487050);
  EXPECT_EQ(segments.Get(12)->range_end_, 487062);
}

TEST_F(DASHTreeTest, CalculateCorrectSegmentNumbersFromSegmentTemplateWithPTO)
{
  tree->SetNowTime(1617223929L);

  OpenTestFile("mpd/segtpl_pto.mpd");

  auto& segments = tree->m_periods[0]->GetAdaptationSets()[0]->GetRepresentations()[0]->SegmentTimeline();

  EXPECT_EQ(segments.GetSize(), 451);
  EXPECT_EQ(segments.Get(0)->range_end_, 404305525);
  EXPECT_EQ(segments.Get(450)->range_end_, 404305975);
}

TEST_F(DASHTreeTest, CalculateCorrectSegmentNumbersFromSegmentTemplateWithOldPublishTime)
{
  tree->SetNowTime(1617229334L);

  OpenTestFile("mpd/segtpl_old_publish_time.mpd");

  auto& segments = tree->m_periods[0]->GetAdaptationSets()[0]->GetRepresentations()[0]->SegmentTimeline();

  EXPECT_EQ(segments.GetSize(), 31);
  EXPECT_EQ(segments.Get(0)->range_end_, 603272);
  EXPECT_EQ(segments.Get(30)->range_end_, 603302);
}

TEST_F(DASHTreeTest, CalculateCorrectFpsScaleFromAdaptionSet)
{
  OpenTestFile("mpd/fps_scale_adaptset.mpd");

  auto& adpSets = tree->m_periods[0]->GetAdaptationSets();

  EXPECT_EQ(adpSets[0]->GetRepresentations()[0]->GetFrameRate(), 24000);
  EXPECT_EQ(adpSets[0]->GetRepresentations()[0]->GetFrameRateScale(), 1001);

  EXPECT_EQ(adpSets[1]->GetRepresentations()[0]->GetFrameRate(), 30);
  EXPECT_EQ(adpSets[1]->GetRepresentations()[0]->GetFrameRateScale(), 1);

  EXPECT_EQ(adpSets[2]->GetRepresentations()[0]->GetFrameRate(), 25);
  EXPECT_EQ(adpSets[2]->GetRepresentations()[0]->GetFrameRateScale(), 1);

  EXPECT_EQ(adpSets[3]->GetRepresentations()[0]->GetFrameRate(), 25000);
  EXPECT_EQ(adpSets[3]->GetRepresentations()[0]->GetFrameRateScale(), 1000);

  EXPECT_EQ(adpSets[4]->GetRepresentations()[0]->GetFrameRate(), 25);
  EXPECT_EQ(adpSets[4]->GetRepresentations()[0]->GetFrameRateScale(), 1);

  EXPECT_EQ(adpSets[5]->GetRepresentations()[0]->GetFrameRate(), 30);
  EXPECT_EQ(adpSets[5]->GetRepresentations()[0]->GetFrameRateScale(), 1);

  EXPECT_EQ(adpSets[6]->GetRepresentations()[0]->GetFrameRate(), 25000);
  EXPECT_EQ(adpSets[6]->GetRepresentations()[0]->GetFrameRateScale(), 1000);
}

TEST_F(DASHTreeAdaptiveStreamTest, replacePlaceHolders)
{
  OpenTestFile("mpd/placeholders.mpd", "https://foo.bar/placeholders.mpd");
  SetTestStream(NewStream(tree->m_periods[0]->GetAdaptationSets()[0].get()));
  
  testStream->start_stream();
  ReadSegments(testStream, 16, 5);
  EXPECT_EQ(testHelper::downloadList[0], "https://foo.bar/videosd-400x224/init.mp4");
  EXPECT_EQ(testHelper::downloadList[4], "https://foo.bar/videosd-400x224/segment_487053.m4s");
  
  SetTestStream(NewStream(tree->m_periods[0]->GetAdaptationSets()[1].get()));
  testStream->start_stream();
  ReadSegments(testStream, 16, 5);
  EXPECT_EQ(testHelper::downloadList[1], "https://foo.bar/videosd-400x224/segment_00487050.m4s");
  EXPECT_EQ(testHelper::downloadList[4], "https://foo.bar/videosd-400x224/segment_00487053.m4s");
 
  
  SetTestStream(NewStream(tree->m_periods[0]->GetAdaptationSets()[2].get()));
  testStream->start_stream();
  ReadSegments(testStream, 16, 5);
  EXPECT_EQ(testHelper::downloadList[1], "https://foo.bar/videosd-400x224/segment_263007000000.m4s");
  EXPECT_EQ(testHelper::downloadList[4], "https://foo.bar/videosd-400x224/segment_263008620000.m4s");

  SetTestStream(NewStream(tree->m_periods[0]->GetAdaptationSets()[3].get()));
  testStream->start_stream();
  ReadSegments(testStream, 16, 5);
  EXPECT_EQ(testHelper::downloadList[1], "https://foo.bar/videosd-400x224/segment_00263007000000");
  EXPECT_EQ(testHelper::downloadList[4], "https://foo.bar/videosd-400x224/segment_00263008620000");

  SetTestStream(NewStream(tree->m_periods[0]->GetAdaptationSets()[4].get()));
  testStream->start_stream();
  ReadSegments(testStream, 16, 5);
  EXPECT_EQ(testHelper::downloadList[1], "https://foo.bar/videosd-400x224/segment_487050.m4s?t=263007000000");
  EXPECT_EQ(testHelper::downloadList[4], "https://foo.bar/videosd-400x224/segment_487053.m4s?t=263008620000");

  SetTestStream(NewStream(tree->m_periods[0]->GetAdaptationSets()[5].get()));
  testStream->start_stream();
  ReadSegments(testStream, 16, 5);
  EXPECT_EQ(testHelper::downloadList[1], "https://foo.bar/videosd-400x224/segment_00487050.m4s?t=00263007000000");
  EXPECT_EQ(testHelper::downloadList[4], "https://foo.bar/videosd-400x224/segment_00487053.m4s?t=00263008620000");

  SetTestStream(NewStream(tree->m_periods[0]->GetAdaptationSets()[6].get()));
  testStream->start_stream();
  ReadSegments(testStream, 16, 5);
  EXPECT_EQ(testHelper::downloadList[0], "https://foo.bar/videosd-400x224/init.mp4");
  EXPECT_EQ(testHelper::downloadList[4], "https://foo.bar/videosd-400x224/segment.m4s");
}

TEST_F(DASHTreeTest, isLiveManifestOnLiveSegmentTimeline)
{
  OpenTestFile("mpd/segtimeline_live_pd.mpd");
  EXPECT_EQ(tree->IsLive(), true);
}

TEST_F(DASHTreeTest, isLiveManifestOnVODSegmentTimeline)
{
  OpenTestFile("mpd/segtimeline_vod.mpd");
  EXPECT_EQ(tree->IsLive(), false);
}

TEST_F(DASHTreeTest, updateParameterLiveSegmentStartNumber)
{
  OpenTestFile("mpd/segtimeline_live_pd.mpd", "https://foo.bar/dash.mpd?foo=bar&baz=qux", {},
               "?start_seq=$START_NUMBER$");
  // Clear testFile to not download the same file as manifest update or will cause mess,
  // the manifest update operation is just to verify the manifest update url
  testHelper::testFile.clear();

  tree->StartManifestUpdate();
  std::string manifestUpdUrl;
  tree->WaitManifestUpdate(manifestUpdUrl);
  EXPECT_EQ(manifestUpdUrl, "https://foo.bar/dash.mpd?start_seq=487063");
}

TEST_F(DASHTreeTest, CalculatePsshDefaultKid)
{
  OpenTestFile("mpd/pssh_default_kid.mpd");

  EXPECT_EQ(tree->m_periods[0]->GetPSSHSets()[1].pssh_, "ABCDEFGH");
  EXPECT_EQ(tree->m_periods[0]->GetPSSHSets()[1].defaultKID_.length(), 16);

  EXPECT_EQ(tree->m_periods[0]->GetPSSHSets()[2].pssh_, "HGFEDCBA");
  EXPECT_EQ(tree->m_periods[0]->GetPSSHSets()[2].defaultKID_.length(), 16);
}

TEST_F(DASHTreeAdaptiveStreamTest, subtitles)
{
  OpenTestFile("mpd/subtitles.mpd", "https://foo.bar/subtitles.mpd");

  auto& adpSets = tree->m_periods[0]->GetAdaptationSets();

  EXPECT_EQ(adpSets[1]->GetStreamType(), PLAYLIST::StreamType::SUBTITLE);
  EXPECT_EQ(adpSets[1]->GetRepresentations()[0]->IsSubtitleFileStream(), true);
  EXPECT_EQ(adpSets[1]->GetRepresentations()[0]->ContainsCodec("ttml"), true);

  EXPECT_EQ(adpSets[2]->GetStreamType(), PLAYLIST::StreamType::SUBTITLE);
  EXPECT_EQ(adpSets[2]->GetRepresentations()[0]->IsSubtitleFileStream(), true);
  EXPECT_EQ(adpSets[2]->GetRepresentations()[0]->ContainsCodec("ttml"), true);

  EXPECT_EQ(adpSets[3]->GetStreamType(), PLAYLIST::StreamType::SUBTITLE);
  EXPECT_EQ(adpSets[3]->GetRepresentations()[0]->IsSubtitleFileStream(), true);
  EXPECT_EQ(adpSets[3]->GetRepresentations()[0]->ContainsCodec("ttml"), true);

  EXPECT_EQ(adpSets[4]->GetStreamType(), PLAYLIST::StreamType::SUBTITLE);
  EXPECT_EQ(adpSets[4]->GetRepresentations()[0]->IsSubtitleFileStream(), true);
  EXPECT_EQ(adpSets[4]->GetRepresentations()[0]->ContainsCodec("ttml"), true);

  EXPECT_EQ(adpSets[5]->GetStreamType(), PLAYLIST::StreamType::SUBTITLE);
  EXPECT_EQ(adpSets[5]->GetRepresentations()[0]->IsSubtitleFileStream(), true);
  EXPECT_EQ(adpSets[5]->GetRepresentations()[0]->ContainsCodec("wvtt"), true);

  EXPECT_EQ(adpSets[6]->GetStreamType(), PLAYLIST::StreamType::SUBTITLE);
  EXPECT_EQ(adpSets[6]->GetRepresentations()[0]->IsSubtitleFileStream(), true);
  EXPECT_EQ(adpSets[6]->GetRepresentations()[0]->ContainsCodec("wvtt"), true);

  EXPECT_EQ(adpSets[7]->GetStreamType(), PLAYLIST::StreamType::SUBTITLE);
  EXPECT_EQ(adpSets[7]->GetRepresentations()[0]->IsSubtitleFileStream(), true);
  EXPECT_EQ(adpSets[7]->GetRepresentations()[0]->ContainsCodec("wvtt"), true);

  EXPECT_EQ(adpSets[8]->GetStreamType(), PLAYLIST::StreamType::SUBTITLE);
  EXPECT_EQ(adpSets[8]->GetRepresentations()[0]->IsSubtitleFileStream(), true);
  EXPECT_EQ(adpSets[8]->GetRepresentations()[0]->ContainsCodec("wvtt"), true);

  EXPECT_EQ(adpSets[9]->GetStreamType(), PLAYLIST::StreamType::SUBTITLE);
  EXPECT_EQ(adpSets[9]->GetRepresentations()[0]->IsSubtitleFileStream(), true);
  EXPECT_EQ(adpSets[9]->GetRepresentations()[0]->ContainsCodec("my_codec"), true);

  EXPECT_EQ(adpSets[10]->GetStreamType(), PLAYLIST::StreamType::SUBTITLE);
  EXPECT_EQ(adpSets[10]->GetRepresentations()[0]->IsSubtitleFileStream(), true);
  EXPECT_EQ(adpSets[10]->GetRepresentations()[0]->ContainsCodec("ttml"), true);

  EXPECT_EQ(adpSets[11]->GetStreamType(), PLAYLIST::StreamType::SUBTITLE);
  EXPECT_EQ(STR(adpSets[11]->GetRepresentations()[0]->GetMimeType()), "application/mp4");
  EXPECT_EQ(adpSets[11]->GetRepresentations()[0]->ContainsCodec("stpp"), true);

  SetTestStream(NewStream(adpSets[11].get()));
  testStream->start_stream();
  ReadSegments(testStream, 16, 5);
  EXPECT_EQ(testHelper::downloadList[0], "https://foo.bar/11/init.mp4");
  EXPECT_EQ(testHelper::downloadList[4], "https://foo.bar/11/0004.m4s");

  EXPECT_EQ(adpSets[12]->GetStreamType(), PLAYLIST::StreamType::SUBTITLE);
  EXPECT_EQ(STR(adpSets[12]->GetMimeType()), "application/mp4");
  EXPECT_EQ(adpSets[12]->GetRepresentations()[0]->ContainsCodec("stpp.ttml.im1t"), true);

  SetTestStream(NewStream(adpSets[12].get()));
  testStream->start_stream();
  ReadSegments(testStream, 16, 5);
  EXPECT_EQ(testHelper::downloadList[0], "https://foo.bar/tears-of-steel-multiple-subtitles-12.dash");
  EXPECT_EQ(testHelper::downloadList[4], "https://foo.bar/tears-of-steel-multiple-subtitles-12-12000.dash");
}

TEST_F(DASHTreeTest, CalculateMultipleSegTpl)
{
  OpenTestFile("mpd/segtpl_multiple.mpd", "https://foo.bar/dash/multiple.mpd");

  EXPECT_EQ(tree->base_url_, "https://foo.bar/dash/");

  auto& adpSets = tree->m_periods[0]->GetAdaptationSets();

  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[0]->GetSegmentTemplate()->GetInitialization()), "https://foo.bar/dash/3c1055cb-a842-4449-b393-7f31693b4a8f_1_448x252init.mp4");
  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[0]->GetSegmentTemplate()->GetMediaUrl()), "https://foo.bar/dash/3c1055cb-a842-4449-b393-7f31693b4a8f_1_448x252_$Number%09d$.mp4");
  EXPECT_EQ(adpSets[0]->GetRepresentations()[0]->GetSegmentTemplate()->GetTimescale(), 120000);
  EXPECT_EQ(adpSets[0]->GetRepresentations()[0]->SegmentTimeline().Get(0)->range_end_, 3);

  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[1]->GetSegmentTemplate()->GetInitialization()), "https://foo.bar/dash/3c1055cb-a842-4449-b393-7f31693b4a8f_2_1920x1080init.mp4");
  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[1]->GetSegmentTemplate()->GetMediaUrl()), "https://foo.bar/dash/3c1055cb-a842-4449-b393-7f31693b4a8f_2_1920x1080_$Number%09d$.mp4");
  EXPECT_EQ(adpSets[0]->GetRepresentations()[1]->GetSegmentTemplate()->GetTimescale(), 90000);
  EXPECT_EQ(adpSets[0]->GetRepresentations()[1]->SegmentTimeline().Get(0)->range_end_, 5);

  EXPECT_EQ(STR(adpSets[1]->GetRepresentations()[0]->GetSegmentTemplate()->GetInitialization()), "https://foo.bar/dash/3c1055cb-a842-4449-b393-7f31693b4a8f_aac1init.mp4");
  EXPECT_EQ(STR(adpSets[1]->GetRepresentations()[0]->GetSegmentTemplate()->GetMediaUrl()), "https://foo.bar/dash/3c1055cb-a842-4449-b393-7f31693b4a8f_aac1_$Number%09d$.mp4");
  EXPECT_EQ(adpSets[1]->GetRepresentations()[0]->GetSegmentTemplate()->GetTimescale(), 48000);
  EXPECT_EQ(adpSets[1]->GetRepresentations()[0]->SegmentTimeline().Get(0)->range_end_, 1);

  EXPECT_EQ(STR(adpSets[2]->GetRepresentations()[0]->GetSegmentTemplate()->GetInitialization()), "https://foo.bar/dash/abc_aac1init.mp4");
  EXPECT_EQ(STR(adpSets[2]->GetRepresentations()[0]->GetSegmentTemplate()->GetMediaUrl()), "https://foo.bar/dash/abc2_$Number%09d$.mp4");
  EXPECT_EQ(adpSets[2]->GetRepresentations()[0]->GetSegmentTemplate()->GetTimescale(), 68000);
  EXPECT_EQ(adpSets[2]->GetRepresentations()[0]->SegmentTimeline().Get(0)->range_end_, 5);
}

TEST_F(DASHTreeTest, CalculateRedirectSegTpl)
{
  testHelper::effectiveUrl = "https://foo.bar/mpd/stream.mpd";
  OpenTestFile("mpd/segtpl.mpd", "https://bit.ly/abcd.mpd");

  auto& adpSets = tree->m_periods[0]->GetAdaptationSets();

  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[0]->GetSegmentTemplate()->GetInitialization()), "https://foo.bar/mpd/V300/init.mp4");
  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[0]->GetSegmentTemplate()->GetMediaUrl()), "https://foo.bar/mpd/V300/$Number$.m4s");

  EXPECT_EQ(STR(adpSets[1]->GetRepresentations()[0]->GetSegmentTemplate()->GetInitialization()), "https://foo.bar/A48/init.mp4");
  EXPECT_EQ(STR(adpSets[1]->GetRepresentations()[0]->GetSegmentTemplate()->GetMediaUrl()), "https://foo.bar/A48/$Number$.m4s");
}

TEST_F(DASHTreeTest, CalculateReprensentationBaseURL)
{
  OpenTestFile("mpd/rep_base_url.mpd", "https://bit.ly/mpd/abcd.mpd");

  auto& adpSets = tree->m_periods[0]->GetAdaptationSets();

  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[0]->GetSegmentTemplate()->GetInitialization()), "https://foo.bar/mpd/slices/A_init.mp4");
  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[0]->GetSegmentTemplate()->GetMediaUrl()), "https://foo.bar/mpd/slices/A$Number%08d$.m4f");
  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[1]->GetSegmentTemplate()->GetInitialization()), "https://bit.ly/mpd/B_init.mp4");
  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[1]->GetSegmentTemplate()->GetMediaUrl()), "https://bit.ly/mpd/B$Number%08d$.m4f");

  EXPECT_EQ(STR(adpSets[1]->GetRepresentations()[0]->GetSegmentTemplate()->GetInitialization()), "https://foo.bar/mpd/slices/A_init.mp4");
  EXPECT_EQ(STR(adpSets[1]->GetRepresentations()[0]->GetSegmentTemplate()->GetMediaUrl()), "https://foo.bar/mpd/slices/A$Number%08d$.m4f");
  EXPECT_EQ(STR(adpSets[1]->GetRepresentations()[1]->GetSegmentTemplate()->GetInitialization()), "https://foo.bar/mpd/slices2/B_init.mp4");
  EXPECT_EQ(STR(adpSets[1]->GetRepresentations()[1]->GetSegmentTemplate()->GetMediaUrl()), "https://foo.bar/mpd/slices2/B$Number%08d$.m4f");
  EXPECT_EQ(STR(adpSets[1]->GetRepresentations()[2]->GetSegmentTemplate()->GetInitialization()), "https://foo.bar/mpd/slices2/C_init.mp4");
  EXPECT_EQ(STR(adpSets[1]->GetRepresentations()[2]->GetSegmentTemplate()->GetMediaUrl()), "https://foo.bar/mpd/slices2/C$Number%08d$.m4f");
}

TEST_F(DASHTreeTest, CalculateReprensentationBaseURLMultiple)
{
  OpenTestFile(
      "mpd/rep_base_url_multiple.mpd",
      "https://pl.foobar.com/assets/p/c30668ab1d7d10166938f06b9643a254.urlset/manifest.mpd");

  auto& adpSets = tree->m_periods[0]->GetAdaptationSets();

  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[0]->GetSegmentTemplate()->GetInitialization()), "https://prod.foobar.com/video/assets/p/c30668ab1d7d10166938f06b9643a254.urlset/init-f1-v1-x3.mp4");
  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[0]->GetSegmentTemplate()->GetMediaUrl()), "https://prod.foobar.com/video/assets/p/c30668ab1d7d10166938f06b9643a254.urlset/fragment-$Number$-f1-v1-x3.m4s");

  EXPECT_EQ(STR(adpSets[1]->GetRepresentations()[0]->GetSegmentTemplate()->GetInitialization()), "https://prod.foobar.com/audio/assets/p/c30668ab1d7d10166938f06b9643a254.urlset/init-f1-a1-x3.mp4");
  EXPECT_EQ(STR(adpSets[1]->GetRepresentations()[0]->GetSegmentTemplate()->GetMediaUrl()), "https://prod.foobar.com/audio/assets/p/c30668ab1d7d10166938f06b9643a254.urlset/fragment-$Number$-f1-a1-x3.m4s");
}

TEST_F(DASHTreeAdaptiveStreamTest, MisalignedSegmentTimeline)
{
  OpenTestFile("mpd/bad_segtimeline_1.mpd", "https://foo.bar/placeholders.mpd");
  SetTestStream(NewStream(tree->m_currentPeriod->GetAdaptationSets()[1].get()));
  testStream->start_stream();

  ReadSegments(testStream, 16, 1);

  testHelper::testFile = "mpd/bad_segtimeline_2.mpd";
  ReadSegments(testStream, 16, 1);
  EXPECT_EQ(tree->m_currentPeriod->GetAdaptationSets()[1]->GetRepresentations()[0]->GetStartNumber(), 3);

  testHelper::testFile = "mpd/bad_segtimeline_3.mpd";
  ReadSegments(testStream, 16, 1);
  EXPECT_EQ(tree->m_currentPeriod->GetAdaptationSets()[1]->GetRepresentations()[0]->GetStartNumber(), 4);

  testHelper::testFile = "mpd/bad_segtimeline_4.mpd";
  ReadSegments(testStream, 16, 1);
  EXPECT_EQ(tree->m_currentPeriod->GetAdaptationSets()[1]->GetRepresentations()[0]->GetStartNumber(), 5);
}

TEST_F(DASHTreeTest, AdaptionSetSwitching)
{
  OpenTestFile("mpd/adaptation_set_switching.mpd");

  auto& adpSets = tree->m_periods[0]->GetAdaptationSets();

  EXPECT_EQ(adpSets.size(), 5);
  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[0]->GetId()), "3");
  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[1]->GetId()), "1");
  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[2]->GetId()), "2");

  EXPECT_EQ(STR(adpSets[1]->GetRepresentations()[0]->GetId()), "4");
  EXPECT_EQ(STR(adpSets[1]->GetRepresentations()[1]->GetId()), "5");

  EXPECT_EQ(STR(adpSets[2]->GetRepresentations()[0]->GetId()), "6");

  EXPECT_EQ(STR(adpSets[3]->GetRepresentations()[0]->GetId()), "7");

  EXPECT_EQ(STR(adpSets[4]->GetRepresentations()[0]->GetId()), "8");
}

TEST_F(DASHTreeTest, SuggestedPresentationDelay)
{
  OpenTestFile("mpd/segtpl_spd.mpd", "https://foo.bar/segtpl_spd.mpd");
  EXPECT_EQ(tree->m_liveDelay, 32);
}

TEST_F(DASHTreeTest, SegmentTemplateStartNumber)
{
  OpenTestFile("mpd/segmenttemplate_startnumber.mpd", "https://vod.service.net/SGP1/highlightpost/1234567890/1/web/dash/segtpl_sn.mpd");

  auto& adpSets = tree->m_periods[0]->GetAdaptationSets();

  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[0]->GetSegmentTemplate()->GetInitialization()), "https://vod.service.net/SGP1/highlightpost/1234567890/1/h264/288000/init_dash.m4v");
  EXPECT_EQ(STR(adpSets[0]->GetRepresentations()[0]->GetSegmentTemplate()->GetMediaUrl()), "https://vod.service.net/SGP1/highlightpost/1234567890/1/h264/288000/seg_dash_$Number$.m4v");
  EXPECT_EQ(adpSets[0]->GetRepresentations()[0]->GetSegmentTemplate()->GetStartNumber(), 0);
  EXPECT_EQ(adpSets[0]->GetRepresentations()[0]->GetSegmentTemplate()->GetTimescale(), 25000);
  EXPECT_EQ(adpSets[0]->GetRepresentations()[0]->GetSegmentTemplate()->GetDuration(), 48000);

  // Verify segments
  auto& rep1Timeline = adpSets[0]->GetRepresentations()[0]->SegmentTimeline();
  EXPECT_EQ(rep1Timeline.GetSize(), 144);

  EXPECT_EQ(rep1Timeline.Get(0)->range_begin_, 0);
  EXPECT_EQ(rep1Timeline.Get(0)->range_end_, 0);

  EXPECT_EQ(rep1Timeline.Get(1)->range_begin_, 48000);
  EXPECT_EQ(rep1Timeline.Get(1)->range_end_, 1);

  EXPECT_EQ(rep1Timeline.Get(143)->range_begin_, 6864000);
  EXPECT_EQ(rep1Timeline.Get(143)->range_end_, 143);
}
