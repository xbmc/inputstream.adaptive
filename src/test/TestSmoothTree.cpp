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


class SmoothTreeTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    UTILS::PROPERTIES::KodiProperties kodiProps;

    m_reprChooser = new CTestRepresentationChooserDefault();
    m_reprChooser->Initialize(kodiProps.m_chooserProps);

    tree = new SmoothTestTree(m_reprChooser);
    tree->Configure(kodiProps);
    tree->m_supportedKeySystem = "urn:uuid:EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED";
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
    SetFileName(testHelper::testFile, filePath);

    tree->SetManifestUpdateParam(url, "");
    if (!tree->open(url, manifestHeaders))
    {
      LOG::Log(LOGERROR, "Cannot open \"%s\" Smooth Streaming manifest.", url.c_str());
      exit(1);
    }
  }

  SmoothTestTree* tree;
  CHOOSER::IRepresentationChooser* m_reprChooser{ nullptr };
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
