/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SSD_dll.h"
#include "common/AdaptiveStream.h"
#include "common/AdaptiveTree.h"
#include "common/RepresentationChooser.h"

#include <float.h>
#include <memory>
#include <vector>

#include <bento4/Ap4.h>
#include <kodi/addon-instance/Inputstream.h>
#include <kodi/tools/DllHelper.h>

class AdaptiveByteStream;
class SampleReader;

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

class ATTR_DLL_LOCAL KodiAdaptiveTree : public adaptive::AdaptiveTree
{
protected:
  virtual bool download(const char* url);
};

class ATTR_DLL_LOCAL KodiAdaptiveStream : public adaptive::AdaptiveStream
{
public:
  KodiAdaptiveStream(adaptive::AdaptiveTree& tree,
                     adaptive::AdaptiveTree::AdaptationSet* adp,
                     const std::map<std::string, std::string>& media_headers,
                     DefaultRepresentationChooser* chooser,
                     bool play_timeshift_buffer,
                     size_t repId,
                     bool choose_rep)
    : adaptive::AdaptiveStream(tree, adp, media_headers, play_timeshift_buffer, repId, choose_rep),
      chooser_(chooser){};

protected:
  bool download(const char* url,
                const std::map<std::string, std::string>& mediaHeaders,
                std::string* lockfreeBuffer) override;
  bool parseIndexRange(adaptive::AdaptiveTree::Representation* rep,
                       const std::string& buffer) override;

private:
  DefaultRepresentationChooser* chooser_ = nullptr;
};

enum MANIFEST_TYPE
{
  MANIFEST_TYPE_UNKNOWN,
  MANIFEST_TYPE_MPD,
  MANIFEST_TYPE_ISM,
  MANIFEST_TYPE_HLS
};

class ATTR_DLL_LOCAL Session : public adaptive::AdaptiveStreamObserver
{
public:
  Session(MANIFEST_TYPE manifestType,
          const std::string& strURL,
          const std::string& strUpdateParam,
          const std::string& strLicType,
          const std::string& strLicKey,
          const std::string& strLicData,
          const std::string& strCert,
          const std::map<std::string, std::string>& manifestHeaders,
          const std::map<std::string, std::string>& mediaHeaders,
          const std::string& profile_path,
          const std::string& ov_audio,
          bool play_timeshift_buffer,
          bool force_secure_decoder,
          const std::string& drm_preinit_data);
  virtual ~Session();
  bool Initialize(const std::uint8_t config, uint32_t max_user_bandwidth);
  bool PreInitializeDRM(std::string& challengeB64, std::string& sessionId);
  bool InitializeDRM();
  bool InitializePeriod();
  SampleReader *GetNextSample();

  struct STREAM
  {
    STREAM(adaptive::AdaptiveTree& t,
           adaptive::AdaptiveTree::AdaptationSet* adp,
           const std::map<std::string, std::string>& media_headers,
           DefaultRepresentationChooser* chooser,
           bool play_timeshift_buffer,
           size_t repId,
           bool choose_rep)
      : enabled(false),
        encrypted(false),
        mainId_(0),
        current_segment_(0),
        m_kodiAdStream(t, adp, media_headers, chooser, play_timeshift_buffer, repId, choose_rep),
        segmentChanged(false),
        valid(true){};

    ~STREAM() { disable(); };

    void disable();
    void reset();

    /*!
     * \brief Get the stream sample reader pointer
     * \return The sample reader, otherwise nullptr if not set
     */
    SampleReader* GetReader() const { return m_streamReader.get(); }

    /*!
     * \brief Set the stream sample reader
     * \param reader The reader
     */
    void SetReader(std::unique_ptr<SampleReader> reader) { m_streamReader = std::move(reader); }

    /*!
     * \brief Get the stream file handler pointer
     * \return The stream file handler, otherwise nullptr if not set
     */
    AP4_File* GetStreamFile() const { return m_streamFile.get(); }

    /*!
     * \brief Set the stream file handler
     * \param streamFile The stream file handler
     */
    void SetStreamFile(std::unique_ptr<AP4_File> streamFile)
    {
      m_streamFile = std::move(streamFile);
    }

    /*!
     * \brief Get the adaptive byte stream handler pointer
     * \return The adaptive byte stream handler, otherwise nullptr if not set
     */
    AdaptiveByteStream* GetAdByteStream() const { return m_adByteStream.get(); }

    /*!
     * \brief Set the adaptive byte stream handler
     * \param dataStream The adaptive byte stream handler
     */
    void SetAdByteStream(std::unique_ptr<AdaptiveByteStream> adByteStream)
    {
      m_adByteStream = std::move(adByteStream);
    }

    bool enabled, encrypted;
    uint16_t mainId_;
    uint32_t current_segment_;
    KodiAdaptiveStream m_kodiAdStream;
    kodi::addon::InputstreamInfo info_;
    bool segmentChanged;
    bool valid;

  private:
    std::unique_ptr<SampleReader> m_streamReader;
    std::unique_ptr<AdaptiveByteStream> m_adByteStream;
    std::unique_ptr<AP4_File> m_streamFile;
  };

  void UpdateStream(STREAM &stream);
  AP4_Movie* PrepareStream(STREAM* stream, bool& needRefetch);

  STREAM* GetStream(unsigned int sid) const
  {
    return sid - 1 < m_streams.size() ? m_streams[sid - 1].get() : nullptr;
  }
  void EnableStream(STREAM* stream, bool enable);
  unsigned int GetStreamCount() const { return m_streams.size(); };
  const char *GetCDMSession(int nSet) { return cdm_sessions_[nSet].cdm_session_str_; };;
  uint8_t GetMediaTypeMask() const { return media_type_mask_; };
  AP4_CencSingleSampleDecrypter * GetSingleSampleDecryptor(unsigned int nIndex)const{ return cdm_sessions_[nIndex].single_sample_decryptor_; };
  SSD::SSD_DECRYPTER *GetDecrypter() { return decrypter_; };
  AP4_CencSingleSampleDecrypter *GetSingleSampleDecrypter(std::string sessionId);
  const SSD::SSD_DECRYPTER::SSD_CAPS &GetDecrypterCaps(unsigned int nIndex) const{ return cdm_sessions_[nIndex].decrypter_caps_; };
  uint64_t GetTotalTimeMs()const { return adaptiveTree_->overallSeconds_ * 1000; };
  uint64_t GetElapsedTimeMs()const { return elapsed_time_ / 1000; };
  uint64_t PTSToElapsed(uint64_t pts);
  uint64_t GetTimeshiftBufferStart();
  void StartReader(
      STREAM* stream, uint64_t seekTimeCorrected, int64_t ptsDiff, bool preceeding, bool timing);
  bool CheckChange(bool bSet = false){ bool ret = changed_; changed_ = bSet; return ret; };
  void SetVideoResolution(unsigned int w, unsigned int h);
  bool SeekTime(double seekTime, unsigned int streamId = 0, bool preceeding=true);
  bool IsLive() const { return adaptiveTree_->has_timeshift_buffer_; };
  MANIFEST_TYPE GetManifestType() const { return manifest_type_; };
  const AP4_UI08 *GetDefaultKeyId(const uint16_t index) const;
  uint32_t GetIncludedStreamMask() const;
  STREAM_CRYPTO_KEY_SYSTEM GetCryptoKeySystem() const;
  uint32_t GetInitialSequence() const { return adaptiveTree_->initial_sequence_; }

  int GetChapter() const;
  int GetChapterCount() const;
  const char* GetChapterName(int ch) const;
  int64_t GetChapterPos(int ch) const;
  int GetPeriodId() const;
  bool SeekChapter(int ch);
  uint64_t GetChapterStartTime() const;
  double GetChapterSeekTime() { return chapter_seek_time_; };
  void ResetChapterSeekTime() { chapter_seek_time_ = 0; };

  //Observer Section
  void OnSegmentChanged(adaptive::AdaptiveStream* adStream) override;
  void OnStreamChange(adaptive::AdaptiveStream* adStream) override;

protected:
  void CheckFragmentDuration(STREAM &stream);
  void GetSupportedDecrypterURN(std::string &key_system);
  void DisposeSampleDecrypter();
  void DisposeDecrypter();

private:
  MANIFEST_TYPE manifest_type_;
  std::string manifestURL_, manifestUpdateParam_;
  std::string license_key_, license_type_, license_data_;
  std::string drmPreInitData_;
  std::map<std::string, std::string> media_headers_;
  AP4_DataBuffer server_certificate_;
  std::string profile_path_;
  std::string ov_audio_;
  kodi::tools::CDllHelper* decrypterModule_;
  SSD::SSD_DECRYPTER *decrypter_;

  struct CDMSESSION
  {
    SSD::SSD_DECRYPTER::SSD_CAPS decrypter_caps_;
    AP4_CencSingleSampleDecrypter *single_sample_decryptor_;
    const char* cdm_session_str_ = nullptr;
    bool shared_single_sample_decryptor_ = false;
  };
  std::vector<CDMSESSION> cdm_sessions_;

  adaptive::AdaptiveTree* adaptiveTree_;
  DefaultRepresentationChooser* representationChooser_;

  std::vector<std::unique_ptr<STREAM>> m_streams;
  STREAM* timing_stream_;

  uint32_t fixed_bandwidth_;
  bool changed_;
  int manual_streams_;
  uint64_t elapsed_time_, chapter_start_time_; // In STREAM_TIME_BASE
  double chapter_seek_time_; // In seconds
  uint8_t media_type_mask_;
  uint8_t drmConfig_;
  bool play_timeshift_buffer_;
  bool force_secure_decoder_;
  bool allow_no_secure_decoder_;
  bool first_period_initialized_;
};
