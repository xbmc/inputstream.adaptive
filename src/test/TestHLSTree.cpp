/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TestHelper.h"
#include "../CompKodiProps.h"
#include "../SrvBroker.h"

#include <gtest/gtest.h>


class HLSTreeTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_reprChooser = new CTestRepresentationChooserDefault();
    tree = new HLSTestTree();
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

  void OpenTestFileMaster(std::string filePath)
  {
    OpenTestFileMaster(filePath, "http://foo.bar/" + filePath);
  }

  bool OpenTestFileMaster(std::string filePath, std::string url)
  {
    return OpenTestFileMaster(filePath, url, {}, std::vector<std::string_view>{});
  }

  bool OpenTestFileMaster(std::string filePath,
                          std::string url,
                          std::map<std::string, std::string> manifestHeaders,
                          std::vector<std::string_view> supportedKeySystems)
  {
    testHelper::testFile = filePath;

    CSrvBroker::GetInstance()->Init({});

    // Download the manifest
    UTILS::CURL::HTTPResponse resp;
    if (!testHelper::DownloadFile(url, {}, {}, resp))
    {
      LOG::Log(LOGERROR, "Cannot download \"%s\" DASH manifest file.", url.c_str());
      return false;
    }

    ADP::KODI_PROPS::ChooserProps chooserProps;
    m_reprChooser->Initialize(chooserProps);
    // We set the download speed to calculate the initial network bandwidth
    m_reprChooser->SetDownloadSpeed(500000);

    tree->Configure(m_reprChooser, supportedKeySystems, "");

    // Parse the manifest
    if (!tree->Open(resp.effectiveUrl, resp.headers, resp.data))
    {
      LOG::Log(LOGERROR, "Cannot open \"%s\" HLS manifest.", url.c_str());
      return false;
    }

    tree->PostOpen();
    tree->m_currentAdpSet = tree->m_periods[0]->GetAdaptationSets()[0].get();
    tree->m_currentRepr = tree->m_currentAdpSet->GetRepresentations()[0].get();
    return true;
  }

  bool OpenTestFileVariant(std::string filePath,
                           std::string url,
                           PLAYLIST::CPeriod* per,
                           PLAYLIST::CAdaptationSet* adp,
                           PLAYLIST::CRepresentation* rep)
  {
    if (!url.empty())
      rep->SetSourceUrl(url);

    testHelper::testFile = filePath;
    return tree->PrepareRepresentation(per, adp, rep);
  }

  adaptive::CHLSTree* tree;
  CHOOSER::IRepresentationChooser* m_reprChooser{nullptr};
};


TEST_F(HLSTreeTest, CalculateSourceUrl)
{
  OpenTestFileMaster("hls/1a2v_master.m3u8", "https://foo.bar/master.m3u8?param=foo");

  bool ret = OpenTestFileVariant("hls/fmp4_noenc_v_stream_2.m3u8", "https://foo.bar/stream_2/out.m3u8",
                                 tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);
  EXPECT_EQ(ret, true);

  std::string rep_url = tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl();
  EXPECT_EQ(rep_url, "https://foo.bar/stream_2/out.m3u8");
}

TEST_F(HLSTreeTest, CalculateSourceUrlFromRedirectedMasterRelativeUri)
{
  testHelper::effectiveUrl = "https://foo.bar/master.m3u8";

  OpenTestFileMaster("hls/1a2v_master.m3u8", "https://baz.qux/master.m3u8");

  std::string rep_url = tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl();

  EXPECT_EQ(rep_url, "https://foo.bar/stream_2/out.m3u8");

  bool ret = OpenTestFileVariant("hls/fmp4_noenc_v_stream_2.m3u8", "https://foo.bar/stream_2/out.m3u8", tree->m_currentPeriod,
                                 tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(ret, true);

  rep_url = tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl();
  EXPECT_EQ(rep_url, "https://foo.bar/stream_2/out.m3u8");
}

TEST_F(HLSTreeTest, CalculateSourceUrlFromRedirectedVariantAbsoluteUri)
{
  OpenTestFileMaster("hls/redirect_absolute_1v_master.m3u8", "https://baz.qux/master.m3u8");

  std::string rep_url = tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl();

  EXPECT_EQ(rep_url, "https://bit.ly/abcd");

  testHelper::effectiveUrl = "https://foo.bar/stream_2/out.m3u8";

  bool ret = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://bit.ly/abcd",
      tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(ret, true);

  rep_url = tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl();
  EXPECT_EQ(rep_url, "https://bit.ly/abcd");
}

TEST_F(HLSTreeTest, CalculateSourceUrlFromRedirectedMasterAndRedirectedVariantAbsoluteUri)
{
  testHelper::effectiveUrl = "https://baz.qux/master.m3u8";

  OpenTestFileMaster("hls/redirect_absolute_1v_master.m3u8", "https://link.to/1234");

  std::string rep_url = tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl();

  EXPECT_EQ(rep_url, "https://bit.ly/abcd");

  testHelper::effectiveUrl = "https://foo.bar/stream_2/out.m3u8";

  bool ret = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://bit.ly/abcd", tree->m_currentPeriod,
      tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(ret, true);

  rep_url = tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl();
  EXPECT_EQ(rep_url, "https://bit.ly/abcd");
}

TEST_F(HLSTreeTest,
       CalculateSourceUrlFromRedirectedMasterAndRedirectedVariantAbsoluteUriSameDomains)
{
  testHelper::effectiveUrl = "https://baz.qux/master.m3u8";

  OpenTestFileMaster("hls/redirect_absolute_1v_master.m3u8", "https://bit.ly/1234");

  std::string rep_url = tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl();

  EXPECT_EQ(rep_url, "https://bit.ly/abcd");

  testHelper::effectiveUrl = "https://foo.bar/stream_2/out.m3u8";

  bool ret = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://bit.ly/abcd", tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(ret, true);

  rep_url = tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl();
  EXPECT_EQ(rep_url, "https://bit.ly/abcd");
}

TEST_F(HLSTreeTest, OpenVariant)
{
  OpenTestFileMaster("hls/1a2v_master.m3u8", "https://foo.bar/master.m3u8");

  bool ret = OpenTestFileVariant(
      "hls/fmp4_noenc_v_stream_2.m3u8", "https://foo.bar/stream_2.m3u8", tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(ret, true);
  EXPECT_EQ(tree->base_url_, "https://foo.bar/");
}

TEST_F(HLSTreeTest, ParseKeyUriStartingWithSlash)
{
  OpenTestFileMaster("hls/1v_master.m3u8", "https://foo.bar/hls/video/stream_name/master.m3u8");

  bool ret = OpenTestFileVariant(
      "hls/ts_aes_keyuriwithslash_stream_0.m3u8",
      "https://foo.bar/hls/video/stream_name/chunklist.m3u8", tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(ret, true);
  std::string licUrl = tree->m_currentPeriod->GetPSSHSets()[1].m_licenseUrl;
  EXPECT_EQ(licUrl, "https://foo.bar/hls/key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, ParseKeyUriStartingWithSlashFromRedirect)
{
  testHelper::effectiveUrl = "https://foo.bar/hls/video/stream_name/master.m3u8";

  OpenTestFileMaster("hls/1v_master.m3u8", "https://baz.qux/hls/video/stream_name/master.m3u8");

  bool ret = OpenTestFileVariant(
      "hls/ts_aes_keyuriwithslash_stream_0.m3u8",
      "https://foo.bar/hls/video/stream_name/chunklist.m3u8", tree->m_currentPeriod,
      tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(ret, true);
  std::string licUrl = tree->m_currentPeriod->GetPSSHSets()[1].m_licenseUrl;
  EXPECT_EQ(licUrl, "https://foo.bar/hls/key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, ParseKeyUriAbsolute)
{
  OpenTestFileMaster("hls/1v_master.m3u8", "https://foo.bar/hls/video/stream_name/master.m3u8");

  bool ret = OpenTestFileVariant(
      "hls/ts_aes_keyuriabsolute_stream_0.m3u8",
      "https://foo.bar/hls/video/stream_name/chunklist.m3u8", tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(ret, true);
  EXPECT_EQ(tree->m_currentPeriod->GetPSSHSets()[1].m_licenseUrl,
            "https://foo.bar/hls/key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, ParseKeyUriRelative)
{
  OpenTestFileMaster("hls/1v_master.m3u8", "https://foo.bar/hls/video/stream_name/master.m3u8");

  bool ret = OpenTestFileVariant(
      "hls/ts_aes_keyurirelative_stream_0.m3u8",
      "https://foo.bar/hls/video/stream_name/chunklist.m3u8", tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(ret, true);
  std::string licUrl = tree->m_currentPeriod->GetPSSHSets()[1].m_licenseUrl;
  EXPECT_EQ(licUrl, "https://foo.bar/hls/key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, ParseKeyUriRelativeFromRedirect)
{
  testHelper::effectiveUrl = "https://foo.bar/hls/video/stream_name/master.m3u8";

  OpenTestFileMaster("hls/1v_master.m3u8", "https://baz.qux/hls/video/stream_name/master.m3u8");
  std::string var_download_url = tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl(); // https://baz.qux/hls/video/stream_name/ts_aes_uriwithslash_chunklist.m3u8

  bool ret = OpenTestFileVariant(
      "hls/ts_aes_keyurirelative_stream_0.m3u8",
      var_download_url,
      tree->m_currentPeriod,
      tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(ret, true);
  std::string licUrl = tree->m_currentPeriod->GetPSSHSets()[1].m_licenseUrl;
  EXPECT_EQ(licUrl, "https://foo.bar/hls/key/key.php?stream=stream_name");
}

TEST_F(HLSTreeTest, PtsSetInMultiPeriod)
{
  OpenTestFileMaster("hls/1a2v_master.m3u8", "https://foo.bar/master.m3u8");
  std::string var_download_url = tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl();

  {
    auto& periodFirst = tree->m_periods[0];

    bool ret =
        OpenTestFileVariant("hls/disco_fmp4_noenc_v_stream_1.m3u8", var_download_url,
                            periodFirst.get(), periodFirst->GetAdaptationSets()[0].get(),
                            periodFirst->GetAdaptationSets()[0]->GetRepresentations()[0].get());

    EXPECT_EQ(ret, true);

    auto& periodSecond = tree->m_periods[1];
    auto& adp0rep1 = periodSecond->GetAdaptationSets()[0]->GetRepresentations()[0];
    auto adp0rep1seg1 = adp0rep1->Timeline().GetFront();
    EXPECT_EQ(adp0rep1seg1->startPTS_, 0);
  }
  {
    auto& periodFirst = tree->m_periods[0];
    var_download_url =
        tree->m_currentPeriod->GetAdaptationSets()[1]->GetRepresentations()[0]->GetSourceUrl();
    bool ret =
        OpenTestFileVariant("hls/disco_fmp4_noenc_a_stream_0.m3u8", var_download_url,
                              periodFirst.get(), periodFirst->GetAdaptationSets()[1].get(),
                              periodFirst->GetAdaptationSets()[1]->GetRepresentations()[0].get());

    EXPECT_EQ(ret, true);

    auto& periodSecond = tree->m_periods[1];
    auto& adp1rep0 = periodSecond->GetAdaptationSets()[1]->GetRepresentations()[0];
    auto adp1rep0seg1 = adp1rep0->Timeline().GetFront();
    EXPECT_EQ(adp1rep0seg1->startPTS_, 0);
  }
}

TEST_F(HLSTreeTest, MultipleEncryptionSequence)
{
  testHelper::effectiveUrl = "https://foo.bar/hls/video/stream_name/master.m3u8";

  OpenTestFileMaster("hls/encrypt_master.m3u8", "https://baz.qux/hls/video/stream_name/master.m3u8");
  std::string var_download_url =
      tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl();

  bool ret = OpenTestFileVariant("hls/encrypt_seq_stream.m3u8", var_download_url,
                                 tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(ret, true);
  auto& periods = tree->m_periods;
  EXPECT_EQ(periods.size(), 3);

  // Check if each period has the period encryption state
  EXPECT_EQ(periods[0]->GetEncryptionState(), PLAYLIST::EncryptionState::UNENCRYPTED);
  EXPECT_EQ(periods[1]->GetEncryptionState(), PLAYLIST::EncryptionState::ENCRYPTED_CK);
  EXPECT_EQ(periods[2]->GetEncryptionState(), PLAYLIST::EncryptionState::UNENCRYPTED);
}

TEST_F(HLSTreeTest, MultipleEncryptionSequenceDrmNoKSMaster)
{
  // Open the master manifest without any supported key system
  // OpenTestFileMaster must return false to prevent ISA to process child manifests
  // since master manifest contains EXT-X-SESSION-KEY used to check supported ks's
  testHelper::effectiveUrl = "https://foo.bar/hls/video/stream_name/master.m3u8";

  bool ret = OpenTestFileMaster("hls/encrypt_master_drm.m3u8",
                                "https://baz.qux/hls/video/stream_name/master.m3u8", {}, std::vector<std::string_view>{});
  EXPECT_EQ(ret, false);
}

TEST_F(HLSTreeTest, MultipleEncryptionSequenceDrmNoKS)
{
  // Open the master manifest without any supported key system
  // OpenTestFileMaster must return true and process child manifests
  // since master manifest DO NOT contains EXT-X-SESSION-KEY
  testHelper::effectiveUrl = "https://foo.bar/hls/video/stream_name/master.m3u8";

  bool ret = OpenTestFileMaster("hls/encrypt_master.m3u8",
                                "https://baz.qux/hls/video/stream_name/master.m3u8", {}, std::vector<std::string_view>{});

  EXPECT_EQ(ret, true);

  std::string var_download_url =
      tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl();

  ret = OpenTestFileVariant("hls/encrypt_seq_stream_drm.m3u8", var_download_url,
                            tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(ret, true);
  auto& periods = tree->m_periods;
  EXPECT_EQ(periods.size(), 2);

  // Check if each period has the period encryption state
  EXPECT_EQ(periods[0]->GetEncryptionState(), PLAYLIST::EncryptionState::NOT_SUPPORTED);
  EXPECT_EQ(periods[1]->GetEncryptionState(), PLAYLIST::EncryptionState::NOT_SUPPORTED);
}

TEST_F(HLSTreeTest, MultipleEncryptionSequenceDrm)
{
  // Open the master manifest with the supported Widevine key system
  // OpenTestFileMaster must return true and process child manifests
  testHelper::effectiveUrl = "https://foo.bar/hls/video/stream_name/master.m3u8";

  bool ret = OpenTestFileMaster("hls/encrypt_master_drm.m3u8",
    "https://baz.qux/hls/video/stream_name/master.m3u8", {}, std::vector<std::string_view>{URN_WIDEVINE});

  EXPECT_EQ(ret, true);

  std::string var_download_url =
      tree->m_currentPeriod->GetAdaptationSets()[0]->GetRepresentations()[0]->GetSourceUrl();

  ret = OpenTestFileVariant("hls/encrypt_seq_stream_drm.m3u8", var_download_url,
                            tree->m_currentPeriod, tree->m_currentAdpSet, tree->m_currentRepr);

  EXPECT_EQ(ret, true);
  auto& periods = tree->m_periods;
  EXPECT_EQ(periods.size(), 2);

  // Check if each period has the period encryption state
  EXPECT_EQ(periods[0]->GetEncryptionState(), PLAYLIST::EncryptionState::ENCRYPTED_DRM);
  EXPECT_EQ(periods[1]->GetEncryptionState(), PLAYLIST::EncryptionState::ENCRYPTED_DRM);
}
