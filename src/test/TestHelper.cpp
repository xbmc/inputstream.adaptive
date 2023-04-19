/*
 *  Copyright (C) 2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TestHelper.h"

std::string testHelper::testFile;
std::string testHelper::effectiveUrl;
std::vector<std::string> testHelper::downloadList;

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

  testHelper::downloadList.push_back(downloadInfo.m_url);

  {
    std::lock_guard<std::mutex> lckrw(thread_data_->mutex_rw_);

    if (state_ == STOPPED)
      return false;

    std::string& segmentBuffer = downloadInfo.m_segmentBuffer->buffer;
    std::string sampleData = "Sixteen bytes!!!";

    tree_.OnDataArrived(downloadInfo.m_segmentBuffer->segment_number,
                        downloadInfo.m_segmentBuffer->segment.pssh_set_, m_decrypterIv,
                        reinterpret_cast<const uint8_t*>(sampleData.data()), segmentBuffer, 0,
                        sampleData.size(), false);
  }
  thread_data_->signal_rw_.notify_all();
  return true;
}

bool TestAdaptiveStream::Download(const DownloadInfo& downloadInfo, std::string& data)
{
  data = "Sixteen bytes!!!";
  return true;
}

void AESDecrypter::decrypt(const AP4_UI08* aes_key,
                           const AP4_UI08* aes_iv,
                           const AP4_UI08* src,
                           std::string& dst,
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

bool AESDecrypter::RenewLicense(const std::string& pluginUrl){return false;}

bool DownloadFile(const std::string& url,
                  const std::map<std::string, std::string>& reqHeaders,
                  std::string& data,
                  adaptive::HTTPRespHeaders& respHeaders)
{
  FILE* f = fopen(testHelper::testFile.c_str(), "rb");
  if (!f)
    return false;

  if (!testHelper::effectiveUrl.empty())
    respHeaders.m_effectiveUrl = testHelper::effectiveUrl;
  else
    respHeaders.m_effectiveUrl = url;

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
  return true;
}

bool DASHTestTree::download(const std::string& url,
                            const std::map<std::string, std::string>& reqHeaders,
                            std::string& data,
                            adaptive::HTTPRespHeaders& respHeaders)
{
  if (DownloadFile(url, reqHeaders, data, respHeaders))
  {
    // We set the download speed to calculate the initial network bandwidth
    m_reprChooser->SetDownloadSpeed(500000);

    return true;
  }
  return false;
}

HLSTestTree::HLSTestTree(CHOOSER::IRepresentationChooser* reprChooser)
  : CHLSTree(reprChooser) 
{
  m_decrypter = std::make_unique<AESDecrypter>(AESDecrypter(std::string()));
}

bool HLSTestTree::download(const std::string& url,
                           const std::map<std::string, std::string>& reqHeaders,
                           std::string& data,
                           adaptive::HTTPRespHeaders& respHeaders)
{
  if (DownloadFile(url, reqHeaders, data, respHeaders))
  {
    // We set the download speed to calculate the initial network bandwidth
    m_reprChooser->SetDownloadSpeed(500000);

    return true;
  }
  return false;
}

bool SmoothTestTree::download(const std::string& url,
                              const std::map<std::string, std::string>& reqHeaders,
                              std::string& data,
                              adaptive::HTTPRespHeaders& respHeaders)
{
  if (DownloadFile(url, reqHeaders, data, respHeaders))
  {
    // We set the download speed to calculate the initial network bandwidth
    m_reprChooser->SetDownloadSpeed(500000);

    return true;
  }
  return false;
}
