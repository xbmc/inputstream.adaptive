/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "../CompKodiProps.h"
#include "../SrvBroker.h"
#include "../decrypters/Helpers.h"
#include "../utils/UrlUtils.h"
#include "TestHelper.h"

#include <gtest/gtest.h>


class SmoothTreeTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_reprChooser = new CTestRepresentationChooserDefault();
    tree = new SmoothTestTree();
  }

  void TearDown() override
  {
    tree->Uninitialize();
    testHelper::effectiveUrl.clear();
    delete tree;
    tree = nullptr;
    delete m_reprChooser;
    m_reprChooser = nullptr;
  }

  void OpenTestFile(std::string filePath) { OpenTestFile(filePath, "http://foo.bar/" + filePath); }

  void OpenTestFile(std::string filePath, std::string url) { OpenTestFile(filePath, url, {}); }

  void OpenTestFile(std::string filePath,
                    std::string url,
                    std::map<std::string, std::string> manifestHeaders)
  {
    testHelper::testFile = filePath;

    CSrvBroker::GetInstance()->Init({});

    // Download the manifest
    UTILS::CURL::HTTPResponse resp;
    if (!testHelper::DownloadFile(url, {}, {}, resp))
    {
      LOG::Log(LOGERROR, "Cannot download \"%s\" DASH manifest file.", url.c_str());
      exit(1);
    }

    ADP::KODI_PROPS::ChooserProps chooserProps;
    m_reprChooser->Initialize(chooserProps);
    // We set the download speed to calculate the initial network bandwidth
    m_reprChooser->SetDownloadSpeed(500000);

    tree->Configure(m_reprChooser, std::vector<std::string_view>{DRM::URN_WIDEVINE}, "");

    // Parse the manifest
    if (!tree->Open(resp.effectiveUrl, resp.headers, resp.data))
    {
      LOG::Log(LOGERROR, "Cannot open \"%s\" Smooth Streaming manifest.", url.c_str());
      exit(1);
    }
    tree->PostOpen();
  }

  SmoothTestTree* tree;
  CHOOSER::IRepresentationChooser* m_reprChooser{nullptr};
};

TEST_F(SmoothTreeTest, CalculateBaseURL)
{
  // No BaseURL tags
  OpenTestFile("ism/TearsOfSteel.ism", "http://amssamples.streaming.mediaservices.windows.net/bc57e088-27ec-44e0-ac20-a85ccbcd50da/TearsOfSteel.ism");
  EXPECT_EQ(tree->base_url_, "http://amssamples.streaming.mediaservices.windows.net/bc57e088-27ec-44e0-ac20-a85ccbcd50da/");
}

TEST_F(SmoothTreeTest, CalculateBaseURLWithNoExtension)
{
  // No BaseURL tags
  OpenTestFile("ism/TearsOfSteel.ism", "http://amssamples.streaming.mediaservices.windows.net/bc57e088-27ec-44e0-ac20-a85ccbcd50da/TearsOfSteel.ism/manifest");
  EXPECT_EQ(tree->base_url_, "http://amssamples.streaming.mediaservices.windows.net/bc57e088-27ec-44e0-ac20-a85ccbcd50da/TearsOfSteel.ism/");
}

TEST_F(SmoothTreeTest, CheckAsyncTimelineStartPTS)
{
  OpenTestFile("ism/live_async_streams.ism", "http://amssamples.streaming.mediaservices.windows.net/bc57e088-27ec-44e0-ac20-a85ccbcd50da/live_async_streams.ism/manifest");

  // Each <StreamIndex> start with different chunk timestamp
  // so to sync streams we adjust PTS with <StreamIndex> which has the lowest timestamp (CSmoothTree::m_ptsBase)
  auto& period = tree->m_periods[0];
  auto& segTL = period->GetAdaptationSets()[0]->GetRepresentations()[0]->Timeline();

  EXPECT_EQ(segTL.GetSize(), 30);
  EXPECT_EQ(segTL.Get(0)->startPTS_, 7058030);
  EXPECT_EQ(segTL.Get(0)->m_time, 3903180167058030);
  EXPECT_EQ(segTL.Get(0)->m_number, 1);

  segTL = period->GetAdaptationSets()[1]->GetRepresentations()[0]->Timeline();

  EXPECT_EQ(segTL.GetSize(), 30);
  EXPECT_EQ(segTL.Get(0)->startPTS_, 71363);
  EXPECT_EQ(segTL.Get(0)->m_time, 3903180160071363);
  EXPECT_EQ(segTL.Get(0)->m_number, 1);

  segTL = period->GetAdaptationSets()[3]->GetRepresentations()[0]->Timeline();

  EXPECT_EQ(segTL.GetSize(), 29);
  EXPECT_EQ(segTL.Get(0)->startPTS_, 0);
  EXPECT_EQ(segTL.Get(0)->m_time, 3903180160000000);
  EXPECT_EQ(segTL.Get(0)->m_number, 1);
}
