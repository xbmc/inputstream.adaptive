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
#include "../common/RepresentationChooser.h"
#include "../parser/DASHTree.h"
#include "../parser/HLSTree.h"
#include "../utils/PropertiesUtils.h"

std::string GetEnv(const std::string& var);
void SetFileName(std::string& file, const std::string name);

struct DefaultRepresentationChooser;

class testHelper
{
public:
  static std::string testFile;
  static std::string effectiveUrl;
  static std::vector<std::string> downloadList;
};

class TestAdaptiveStream : public adaptive::AdaptiveStream
{
public:
  TestAdaptiveStream(adaptive::AdaptiveTree& tree,
    adaptive::AdaptiveTree::AdaptationSet* adp,
    const std::map<std::string, std::string>& media_headers,
    DefaultRepresentationChooser* chooser,
    bool play_timeshift_buffer,
    size_t repId,
    bool choose_rep)
    : adaptive::AdaptiveStream(tree, adp, media_headers, play_timeshift_buffer, repId, choose_rep),
    chooser_(chooser) {};
  std::chrono::system_clock::time_point mock_time_stream = std::chrono::system_clock::now();
  void SetLastUpdated(std::chrono::system_clock::time_point tm) override { lastUpdated_ = tm; };
  virtual bool download_segment() override;

protected:
  virtual bool download(const std::string& url,
                        const std::map<std::string, std::string>& mediaHeaders,
                        std::string* lockfreeBuffer) override;

private:
  DefaultRepresentationChooser* chooser_ = nullptr;
};

class AESDecrypter : public IAESDecrypter
{
public:
  AESDecrypter(const std::string& licenseKey) : m_licenseKey(licenseKey){};
  virtual ~AESDecrypter() = default;

  void decrypt(const AP4_UI08* aes_key,
               const AP4_UI08* aes_iv,
               const AP4_UI08* src,
               AP4_UI08* dst,
               size_t dataSize);
  std::string convertIV(const std::string& input);
  void ivFromSequence(uint8_t* buffer, uint64_t sid);
  const std::string& getLicenseKey() const { return m_licenseKey; };
  bool RenewLicense(const std::string& pluginUrl);

private:
  std::string m_licenseKey;
};

class DASHTestTree : public adaptive::DASHTree
{
public:
  DASHTestTree(UTILS::PROPERTIES::KodiProperties kodiProps);
  uint64_t GetNowTime() override { return m_mockTime; }
  void SetNowTime(uint64_t time) { m_mockTime = time; }
  void SetLastUpdated(std::chrono::system_clock::time_point tm) override { lastUpdated_ = tm; };
  std::chrono::system_clock::time_point GetNowTimeChrono() { return m_mock_time_chrono; };

private:
  uint64_t m_mockTime = 10000000L;
  std::chrono::system_clock::time_point m_mock_time_chrono = std::chrono::system_clock::now();
};
