
/*
*      Copyright (C) 2016-2016 peak3d
*      http://www.peak3d.de
*
*  This Program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This Program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  <http://www.gnu.org/licenses/>.
*
*/

#include <vector>

#include "common/AdaptiveTree.h"
#include "common/AdaptiveStream.h"
#include <float.h>

#include "Ap4.h"

#include "kodi_inputstream_types.h"

class FragmentedSampleReader;
class SSD_DECRYPTER;

namespace XBMCFILE
{
  /* indicate that caller can handle truncated reads, where function returns before entire buffer has been filled */
  static const unsigned int READ_TRUNCATED = 0x01;

  /* indicate that that caller support read in the minimum defined chunk size, this disables internal cache then */
  static const unsigned int READ_CHUNKED   = 0x02;

  /* use cache to access this file */
  static const unsigned int READ_CACHED    = 0x04;

  /* open without caching. regardless to file type. */
  static const unsigned int READ_NO_CACHE  = 0x08;

  /* calcuate bitrate for file while reading */
  static const unsigned int READ_BITRATE   = 0x10;
}

/*******************************************************
Kodi Streams implementation
********************************************************/

class KodiAdaptiveTree : public adaptive::AdaptiveTree
{
protected:
  virtual bool download(const char* url);
};

class KodiAdaptiveStream : public adaptive::AdaptiveStream
{
public:
  KodiAdaptiveStream(adaptive::AdaptiveTree &tree, adaptive::AdaptiveTree::StreamType type)
    :adaptive::AdaptiveStream(tree, type){};
protected:
  virtual bool download(const char* url, const std::map<std::string, std::string> &mediaHeaders) override;
  virtual bool parseIndexRange() override;
};

class FragmentObserver
{
public:
  virtual void BeginFragment(AP4_UI32 streamId) = 0;
  virtual void EndFragment(AP4_UI32 streamId) = 0;
};

enum MANIFEST_TYPE
{
  MANIFEST_TYPE_UNKNOWN,
  MANIFEST_TYPE_MPD,
  MANIFEST_TYPE_ISM
};


class Session: public FragmentObserver
{
public:
  Session(MANIFEST_TYPE manifestType, const char *strURL, const char *strLicType, const char* strLicKey, const char* strLicData, const char* strCert, const std::map<std::string, std::string> &manifestHeaders, const std::map<std::string, std::string> &mediaHeaders, const char* profile_path);
  ~Session();
  bool initialize();
  FragmentedSampleReader *GetNextSample();

  struct STREAM
  {
    STREAM(adaptive::AdaptiveTree &t, adaptive::AdaptiveTree::StreamType s) :stream_(t, s), enabled(false), encrypted(false), current_segment_(0), input_(0), reader_(0), input_file_(0) { memset(&info_, 0, sizeof(info_)); };
    ~STREAM() { disable(); free((void*)info_.m_ExtraData); };
    void disable();

    bool enabled, encrypted;
    uint32_t current_segment_;
    KodiAdaptiveStream stream_;
    AP4_ByteStream *input_;
    AP4_File *input_file_;
    INPUTSTREAM_INFO info_;
    FragmentedSampleReader *reader_;
  };

  void UpdateStream(STREAM &stream);

  STREAM *GetStream(unsigned int sid)const { return sid - 1 < streams_.size() ? streams_[sid - 1] : 0; };
  unsigned int GetStreamCount() const { return streams_.size(); };
  const AP4_DataBuffer &GetCryptoData() { return m_cryptoData; };
  uint8_t GetMediaTypeMask() const { return media_type_mask_; };
  std::uint16_t GetWidth()const { return width_; };
  std::uint16_t GetHeight()const { return height_; };
  AP4_CencSingleSampleDecrypter * GetSingleSampleDecryptor()const{ return single_sample_decryptor_; };
  double GetPresentationTimeOffset() { return adaptiveTree_->minPresentationOffset < DBL_MAX? adaptiveTree_->minPresentationOffset:0; };
  double GetTotalTime()const { return adaptiveTree_->overallSeconds_; };
  double GetPTS()const { return last_pts_; };
  bool CheckChange(bool bSet = false){ bool ret = changed_; changed_ = bSet; return ret; };
  void SetVideoResolution(unsigned int w, unsigned int h) { width_ = w < maxwidth_ ? w : maxwidth_; height_ = h < maxheight_ ? h : maxheight_;};
  bool SeekTime(double seekTime, unsigned int streamId = 0, bool preceeding=true);
  bool IsLive() const { return adaptiveTree_->has_timeshift_buffer_; };
  MANIFEST_TYPE GetManifestType() const { return manifest_type_; };
  const AP4_UI08 *GetDefaultKeyId() const;

  //Observer Section
  void BeginFragment(AP4_UI32 streamId) override;
  void EndFragment(AP4_UI32 streamId) override;

protected:
  void GetSupportedDecrypterURN(std::pair<std::string, std::string> &urn);
  AP4_CencSingleSampleDecrypter *CreateSingleSampleDecrypter(AP4_DataBuffer &streamCodec);

private:
  MANIFEST_TYPE manifest_type_;
  std::string mpdFileURL_;
  std::string license_key_, license_type_, license_data_;
  std::map<std::string, std::string> media_headers_;
  AP4_DataBuffer server_certificate_;
  std::string profile_path_;
  void * decrypterModule_;
  SSD_DECRYPTER *decrypter_;
  AP4_DataBuffer m_cryptoData;

  adaptive::AdaptiveTree *adaptiveTree_;

  std::vector<STREAM*> streams_;

  uint16_t width_, height_;
  uint16_t maxwidth_, maxheight_;
  uint32_t fixed_bandwidth_;
  bool changed_;
  bool manual_streams_;
  double last_pts_;
  uint8_t media_type_mask_;

  AP4_CencSingleSampleDecrypter *single_sample_decryptor_;
};
