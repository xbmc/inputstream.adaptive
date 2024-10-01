/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TestHelper.h"

#include "../utils/CurlUtils.h"

std::string testHelper::testFile;
std::string testHelper::effectiveUrl;
std::vector<std::string> testHelper::downloadList;

bool testHelper::LoadFile(std::string path, std::string& data)
{
  // Add project "test/manifests/" data folder path
  path.insert(0, GetEnv("DATADIR") + "/");

  FILE* f = fopen(path.c_str(), "rb");
  if (!f)
  {
    LOG::LogF(LOGERROR, "Failed open file %s", path.c_str());
    return false;
  }

  // read the file
  static const size_t bufferSize{16 * 1024}; // 16 byte
  std::vector<char> bufferData(bufferSize);
  bool isEOF{false};

  while (!isEOF)
  {
    // Read the data in chunks
    size_t byteRead{fread(bufferData.data(), sizeof(char), bufferSize, f)};
    if (byteRead == 0) // EOF
    {
      isEOF = true;
    }
    else
    {
      data.append(bufferData.data(), byteRead);
    }
  }

  fclose(f);
  return isEOF;
}

bool testHelper::DownloadFile(std::string_view url,
                  const std::map<std::string, std::string>& reqHeaders,
                  const std::vector<std::string>& respHeaders,
                  UTILS::CURL::HTTPResponse& resp)
{
  if (testHelper::testFile.empty())
    return false;

  bool ret = LoadFile(testHelper::testFile, resp.data);

  if (!ret)
    return false;

  if (!testHelper::effectiveUrl.empty())
    resp.effectiveUrl = testHelper::effectiveUrl;
  else
    resp.effectiveUrl = url;

  return true;
}

std::string GetEnv(const std::string& var)
{
  const char* val = std::getenv(var.c_str());
  if (val == nullptr)
    return "";
  else
    return val;
}

void SetFileName(std::string& file, std::string name)
{
  file = GetEnv("DATADIR") + "/" + name;
}

bool TestAdaptiveStream::DownloadSegment(const DownloadInfo& downloadInfo)
{
  if (downloadInfo.m_url.empty())
    return false;

  std::vector<uint8_t>& segmentBuffer = downloadInfo.m_segmentBuffer->buffer;
  std::stringstream sampleData("Sixteen bytes!!!");

  const size_t bufferSize = 8;
  uint8_t bufferData[bufferSize];
  size_t totalByteRead = 0;

  sampleData.clear();
  sampleData.seekg(0);

  // Simulate the downloading/reading data in chunks
  while (true)
  {
    {
      std::lock_guard<std::mutex> lckrw(thread_data_->mutex_rw_);

      if (state_ == STOPPED)
        break;

      sampleData.read(reinterpret_cast<char*>(bufferData), bufferSize);
      size_t bytesRead = sampleData.gcount();

      if (bytesRead == 0) // EOF
        break;

      m_tree->OnDataArrived(downloadInfo.m_segmentBuffer->segment_number,
                            downloadInfo.m_segmentBuffer->segment.pssh_set_, m_decrypterIv,
                            bufferData, bytesRead, segmentBuffer, segmentBuffer.size(), false);

      totalByteRead += bytesRead;
    }
  }

  if (totalByteRead == 0)
  {
    LOG::LogF(LOGFATAL, "Cannot read buffer sample data, download cancelled");
    return false;
  }

  testHelper::downloadList.push_back(downloadInfo.m_url);

  thread_data_->signal_rw_.notify_all();
  return true;
}

bool TestAdaptiveStream::Download(const DownloadInfo& downloadInfo, std::vector<uint8_t>& data)
{
  const char* dataStr = "Sixteen bytes!!!";
  data.insert(data.end(), dataStr, dataStr + 16);
  return true;
}

void AESDecrypter::decrypt(const AP4_UI08* aes_key,
                           const AP4_UI08* aes_iv,
                           const AP4_UI08* src,
                           std::vector<uint8_t>& dst,
                           size_t dstOffset,
                           size_t& dataSize,
                           bool lastChunk)
{
}

std::string AESDecrypter::convertIV(const std::string& input)
{
  std::string result;
  return result;
}

void AESDecrypter::ivFromSequence(uint8_t* buffer, uint64_t sid){}

std::string DASHTestTree::RunManifestUpdate(std::string manifestUpdFile)
{
  m_manifestUpdUrl.clear();
  testHelper::testFile = manifestUpdFile;
  OnUpdateSegments();
  return m_manifestUpdUrl;
}

bool DASHTestTree::DownloadManifestUpd(std::string_view url,
                                       const std::map<std::string, std::string>& reqHeaders,
                                       const std::vector<std::string>& respHeaders,
                                       UTILS::CURL::HTTPResponse& resp)
{
  m_manifestUpdUrl = url.data();

  if (testHelper::DownloadFile(url, reqHeaders, respHeaders, resp))
  {
    return true;
  }
  return false;
}

HLSTestTree::HLSTestTree() : CHLSTree() 
{
  m_decrypter = std::make_unique<AESDecrypter>(AESDecrypter(std::string()));
}

bool HLSTestTree::DownloadKey(std::string_view url,
                              const std::map<std::string, std::string>& reqHeaders,
                              const std::vector<std::string>& respHeaders,
                              UTILS::CURL::HTTPResponse& resp)
{
  if (testHelper::DownloadFile(url, reqHeaders, respHeaders, resp))
  {
    return true;
  }
  return false;
}

bool HLSTestTree::DownloadManifestChild(std::string_view url,
                                        const std::map<std::string, std::string>& reqHeaders,
                                        const std::vector<std::string>& respHeaders,
                                        UTILS::CURL::HTTPResponse& resp)
{
  if (testHelper::DownloadFile(url, reqHeaders, respHeaders, resp))
  {
    return true;
  }
  return false;
}
