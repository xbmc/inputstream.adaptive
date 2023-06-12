/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../Iaes_decrypter.h"
#include "../common/AdaptiveStream.h"
#include "../common/Chooser.h"
#include "../common/ChooserDefault.h"
#include "../parser/DASHTree.h"
#include "../parser/HLSTree.h"
#include "../parser/SmoothTree.h"
#include "../utils/log.h"
#include "../utils/PropertiesUtils.h"

#include <condition_variable>
#include <mutex>
#include <string_view>

// \brief Current version of gtest dont support compare std::string_view values
//        this shortens the conversion needed
using STR = std::string;

std::string GetEnv(const std::string& var);
void SetFileName(std::string& file, const std::string name);

class testHelper
{
public:
  static bool LoadFile(std::string path, std::string& data);

  static bool DownloadFile(std::string_view url,
                           const std::map<std::string, std::string>& reqHeaders,
                           const std::vector<std::string>& respHeaders,
                           UTILS::CURL::HTTPResponse& resp);

  static std::string testFile;
  static std::string effectiveUrl;
  static std::vector<std::string> downloadList;
};

class CTestRepresentationChooserDefault : public CHOOSER::CRepresentationChooserDefault
{
public:
  CTestRepresentationChooserDefault() : CHOOSER::CRepresentationChooserDefault() {}
  ~CTestRepresentationChooserDefault() override {}

  void Initialize(const UTILS::PROPERTIES::ChooserProps& props) override
  {
  }
};

class TestAdaptiveStream : public adaptive::AdaptiveStream
{
public:
  TestAdaptiveStream(adaptive::AdaptiveTree& tree,
                     PLAYLIST::CAdaptationSet* adp,
                     PLAYLIST::CRepresentation* initialRepr,
                     const UTILS::PROPERTIES::KodiProperties& kodiProps,
                     bool choose_rep)
    : adaptive::AdaptiveStream(tree, adp, initialRepr, kodiProps, choose_rep)
  {
  }

  std::chrono::system_clock::time_point mock_time_stream = std::chrono::system_clock::now();
  void SetLastUpdated(const std::chrono::system_clock::time_point tm) override
  {
    lastUpdated_ = tm;
  }
  virtual bool DownloadSegment(const DownloadInfo& downloadInfo) override;

protected:
  virtual bool Download(const DownloadInfo& downloadInfo, std::string& data) override;
};

class AESDecrypter : public IAESDecrypter
{
public:
  AESDecrypter(const std::string& licenseKey) : m_licenseKey(licenseKey){};
  virtual ~AESDecrypter() = default;

  void decrypt(const AP4_UI08* aes_key,
               const AP4_UI08* aes_iv,
               const AP4_UI08* src,
               std::string& dst,
               size_t dstOffset,
               size_t& dataSize,
               bool lastChunk);
  std::string convertIV(const std::string& input);
  void ivFromSequence(uint8_t* buffer, uint64_t sid);
  const std::string& getLicenseKey() const { return m_licenseKey; };
  bool RenewLicense(const std::string& pluginUrl);

private:
  std::string m_licenseKey;
};

class DASHTestTree : public adaptive::CDashTree
{
public:
  DASHTestTree() : CDashTree() {}
  uint64_t GetTimestamp() override { return m_mockTime; }
  void SetNowTime(uint64_t time) { m_mockTime = time; }
  void SetLastUpdated(const std::chrono::system_clock::time_point tm) { lastUpdated_ = tm; }
  std::chrono::system_clock::time_point GetNowTimeChrono() { return m_mock_time_chrono; };

  virtual void RefreshLiveSegments() override;

  /*!
   * \brief Start a manifest update, will be executed a single update,
   *        mandatory use WaitGetManifestUpdate method to wait the response data
   */
  void StartManifestUpdate();

  /*!
   * \brief Wait the data response of a manifest update
   * \param url[OUT] Provides the url used to the manifest request
   */
  void WaitManifestUpdate(std::string& url);

private:
  bool DownloadManifestUpd(std::string_view url,
                           const std::map<std::string, std::string>& reqHeaders,
                           const std::vector<std::string>& respHeaders,
                           UTILS::CURL::HTTPResponse& resp) override;

  virtual CDashTree* Clone() const override { return new DASHTestTree{*this}; }

  uint64_t m_mockTime = 10000000L;
  std::chrono::system_clock::time_point m_mock_time_chrono = std::chrono::system_clock::now();

  bool m_isManifestUpdSingleRun{false};
  std::string m_manifestUpdUrl; // Temporarily stores the url where to request the manifest update
  bool m_isManifestUpdReady{false}; // Security check for the variable_condition

  static std::condition_variable s_cvDashManifestUpd;
  static std::mutex s_mutexDashManifestUpd;
};

class HLSTestTree : public adaptive::CHLSTree
{
public:
  HLSTestTree();

  virtual HLSTestTree* Clone() const override { return new HLSTestTree{*this}; }

private:
  bool DownloadKey(std::string_view url,
                   const std::map<std::string, std::string>& reqHeaders,
                   const std::vector<std::string>& respHeaders,
                   UTILS::CURL::HTTPResponse& resp) override;

  bool DownloadManifestChild(std::string_view url,
                             const std::map<std::string, std::string>& reqHeaders,
                             const std::vector<std::string>& respHeaders,
                             UTILS::CURL::HTTPResponse& resp) override;
};

class SmoothTestTree : public adaptive::CSmoothTree
{
public:
  SmoothTestTree() : CSmoothTree() {}
};
