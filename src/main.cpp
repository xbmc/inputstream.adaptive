/*
 *  Copyright (C) 2016 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "main.h"

#include "ADTSReader.h"
#include <bento4/Ap4Utils.h>
#include "TSReader.h"
#include "WebmReader.h"
#include "aes_decrypter.h"
#include "oscompat.h"
#include "codechandler/AVCCodecHandler.h"
#include "codechandler/HEVCCodecHandler.h"
#include "codechandler/MPEGCodecHandler.h"
#include "codechandler/TTMLCodecHandler.h"
#include "codechandler/VP9CodecHandler.h"
#include "codechandler/WebVTTCodecHandler.h"
#include "parser/DASHTree.h"
#include "parser/HLSTree.h"
#include "parser/SmoothTree.h"
#include "utils/Base64Utils.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"

#include <chrono>
#include <algorithm>
#include <iostream>
#include <math.h>
#include <sstream>
#include <stdarg.h> // va_list, va_start, va_arg, va_end
#include <stdio.h>
#include <string.h>

#include <kodi/Filesystem.h>
#include <kodi/General.h>
#include <kodi/addon-instance/VideoCodec.h>
#include <kodi/addon-instance/inputstream/StreamCodec.h>
#include "kodi/tools/StringUtils.h"

#if defined(ANDROID)
#include <kodi/platform/android/System.h>
#endif

#ifdef CreateDirectory
#undef CreateDirectory
#endif

#define STREAM_TIME_BASE 1000000

using namespace UTILS;
using namespace kodi::tools;

static const AP4_Track::Type TIDC[adaptive::AdaptiveTree::STREAM_TYPE_COUNT] = {
    AP4_Track::TYPE_UNKNOWN, AP4_Track::TYPE_VIDEO, AP4_Track::TYPE_AUDIO,
    AP4_Track::TYPE_SUBTITLES};

/*******************************************************
kodi host - interface for decrypter libraries
********************************************************/
class ATTR_DLL_LOCAL KodiHost : public SSD::SSD_HOST
{
public:
#if defined(ANDROID)
  virtual void* GetJNIEnv() override { return m_androidSystem.GetJNIEnv(); };

  virtual int GetSDKVersion() override { return m_androidSystem.GetSDKVersion(); };

  virtual const char* GetClassName() override
  {
    m_retvalHelper = m_androidSystem.GetClassName();
    return m_retvalHelper.c_str();
  };

#endif
  virtual const char* GetLibraryPath() const override { return m_strLibraryPath.c_str(); };

  virtual const char* GetProfilePath() const override { return m_strProfilePath.c_str(); };

  virtual void* CURLCreate(const char* strURL) override
  {
    kodi::vfs::CFile* file = new kodi::vfs::CFile;
    if (!file->CURLCreate(strURL))
    {
      delete file;
      return nullptr;
    }
    return file;
  };

  virtual bool CURLAddOption(void* file,
                             CURLOPTIONS opt,
                             const char* name,
                             const char* value) override
  {
    const CURLOptiontype xbmcmap[] = {ADDON_CURL_OPTION_PROTOCOL, ADDON_CURL_OPTION_HEADER};
    return static_cast<kodi::vfs::CFile*>(file)->CURLAddOption(xbmcmap[opt], name, value);
  }

  virtual const char* CURLGetProperty(void* file, CURLPROPERTY prop, const char* name) override
  {
    const FilePropertyTypes xbmcmap[] = {ADDON_FILE_PROPERTY_RESPONSE_HEADER};
    m_strPropertyValue =
        static_cast<kodi::vfs::CFile*>(file)->GetPropertyValue(xbmcmap[prop], name);
    return m_strPropertyValue.c_str();
  }

  virtual bool CURLOpen(void* file) override
  {
    return static_cast<kodi::vfs::CFile*>(file)->CURLOpen(ADDON_READ_NO_CACHE);
  };

  virtual size_t ReadFile(void* file, void* lpBuf, size_t uiBufSize) override
  {
    return static_cast<kodi::vfs::CFile*>(file)->Read(lpBuf, uiBufSize);
  };

  virtual void CloseFile(void* file) override
  {
    return static_cast<kodi::vfs::CFile*>(file)->Close();
  };

  virtual bool CreateDir(const char* dir) override { return kodi::vfs::CreateDirectory(dir); };

  void LogVA(const SSD::SSDLogLevel level, const char* format, va_list args) override
  {
    std::vector<char> data;
    data.resize(256);

    va_list argsStart;
    va_copy(argsStart, args);
    
    int ret;
    while ((ret = vsnprintf(data.data(), data.size(), format, args)) > data.size())
    {
      data.resize(data.size() * 2);
      args = argsStart;
    }
    LOG::Log(static_cast<LogLevel>(level), data.data());
    va_end(argsStart);
  };

  void SetLibraryPath(const char* libraryPath)
  {
    m_strLibraryPath = libraryPath;

    const char* pathSep(libraryPath[0] && libraryPath[1] == ':' && isalpha(libraryPath[0]) ? "\\"
                                                                                           : "/");

    if (m_strLibraryPath.size() && m_strLibraryPath.back() != pathSep[0])
      m_strLibraryPath += pathSep;
  }

  void SetProfilePath(const std::string& profilePath)
  {
    m_strProfilePath = profilePath;

    const char* pathSep(profilePath[0] && profilePath[1] == ':' && isalpha(profilePath[0]) ? "\\"
                                                                                           : "/");

    if (m_strProfilePath.size() && m_strProfilePath.back() != pathSep[0])
      m_strProfilePath += pathSep;

    //let us make cdm userdata out of the addonpath and share them between addons
    m_strProfilePath.resize(
        m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 2));
    m_strProfilePath.resize(
        m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 1));
    m_strProfilePath.resize(
        m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 1) + 1);

    kodi::vfs::CreateDirectory(m_strProfilePath.c_str());
    m_strProfilePath += "cdm";
    m_strProfilePath += pathSep;
    kodi::vfs::CreateDirectory(m_strProfilePath.c_str());
  }

  virtual bool GetBuffer(void* instance, SSD::SSD_PICTURE& picture) override
  {
    return instance ? static_cast<kodi::addon::CInstanceVideoCodec*>(instance)->GetFrameBuffer(
                          *reinterpret_cast<VIDEOCODEC_PICTURE*>(&picture))
                    : false;
  }

  virtual void ReleaseBuffer(void* instance, void* buffer) override
  {
    if (instance)
      static_cast<kodi::addon::CInstanceVideoCodec*>(instance)->ReleaseFrameBuffer(buffer);
  }

private:
  std::string m_strProfilePath, m_strLibraryPath, m_strPropertyValue;

#if defined(ANDROID)
  kodi::platform::CInterfaceAndroidSystem m_androidSystem;
  std::string m_retvalHelper;
#endif
} * kodihost;

/*******************************************************
Bento4 Streams
********************************************************/

class ATTR_DLL_LOCAL AdaptiveByteStream : public AP4_ByteStream
{
public:
  // Constructor
  AdaptiveByteStream(adaptive::AdaptiveStream* adStream) : m_adStream(adStream){};
  virtual ~AdaptiveByteStream() {};

  // AP4_ByteStream methods
  AP4_Result ReadPartial(void* buffer, AP4_Size bytesToRead, AP4_Size& bytesRead) override
  {
    bytesRead = m_adStream->read(buffer, bytesToRead);
    return bytesRead > 0 ? AP4_SUCCESS : AP4_ERROR_READ_FAILED;
  };
  AP4_Result WritePartial(const void* buffer,
                          AP4_Size bytesToWrite,
                          AP4_Size& bytesWritten) override
  {
    /* unimplemented */
    return AP4_ERROR_NOT_SUPPORTED;
  };
  AP4_Result Seek(AP4_Position position) override
  {
    return m_adStream->seek(position) ? AP4_SUCCESS : AP4_ERROR_NOT_SUPPORTED;
  };
  AP4_Result Tell(AP4_Position& position) override
  {
    position = m_adStream->tell();
    return AP4_SUCCESS;
  };
  AP4_Result GetSize(AP4_LargeSize& size) override { return AP4_ERROR_NOT_SUPPORTED; };
  AP4_Result GetSegmentSize(size_t& size)
  {
    if (m_adStream->ensureSegment() && m_adStream->retrieveCurrentSegmentBufferSize(size))
    {
      return AP4_SUCCESS;
    }
    return AP4_ERROR_EOS;
  };
  // AP4_Referenceable methods
  void AddReference() override{};
  void Release() override{};
  bool waitingForSegment() const { return m_adStream->waitingForSegment(); }
  void FixateInitialization(bool on) { m_adStream->FixateInitialization(on); }
  void SetSegmentFileOffset(uint64_t offset) { m_adStream->SetSegmentFileOffset(offset); }

protected:
  adaptive::AdaptiveStream* m_adStream;
};

/*******************************************************
Kodi Streams implementation
********************************************************/

bool adaptive::AdaptiveTree::download(const std::string& url,
                                      const std::map<std::string, std::string>& manifestHeaders,
                                      void* opaque,
                                      bool isManifest)
{
  // open the file
  kodi::vfs::CFile file;
  if (!file.CURLCreate(url))
    return false;

  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "seekable", "0");
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "acceptencoding", "gzip");

  for (const auto& entry : manifestHeaders)
  {
    file.CURLAddOption(ADDON_CURL_OPTION_HEADER, entry.first.c_str(), entry.second.c_str());
  }

  if (!file.CURLOpen(ADDON_READ_CHUNKED | ADDON_READ_NO_CACHE))
  {
    LOG::Log(LOGERROR, "Download failed: %s", url.c_str());
    return false;
  }

  effective_url_ = file.GetPropertyValue(ADDON_FILE_PROPERTY_EFFECTIVE_URL, "");

  if (isManifest && !PreparePaths(effective_url_))
  {
    file.Close();
    return false;
  }

  // read the file
  static const unsigned int CHUNKSIZE = 16384;
  char buf[CHUNKSIZE];
  size_t nbRead;

  while ((nbRead = file.Read(buf, CHUNKSIZE)) > 0 && ~nbRead && write_data(buf, nbRead, opaque))
    ;

  etag_ = file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, "etag");
  last_modified_ = file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER, "last-modified");

  //download_speed_ = file.GetFileDownloadSpeed();

  file.Close();

  LOG::Log(LOGDEBUG, "Download finished: %s", effective_url_.c_str());

  return nbRead == 0;
}

bool KodiAdaptiveStream::download(const std::string& url,
                                  const std::map<std::string, std::string>& mediaHeaders,
                                  std::string* lockfreeBuffer)
{
  kodi::vfs::CFile file;

  // open the file
  if (!file.CURLCreate(url))
    return false;
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "seekable", "0");
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "acceptencoding", "gzip, deflate");
  if (mediaHeaders.find("connection") == mediaHeaders.end())
    file.CURLAddOption(ADDON_CURL_OPTION_HEADER, "connection", "keep-alive");
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "failonerror", "false");

  for (const auto& entry : mediaHeaders)
  {
    file.CURLAddOption(ADDON_CURL_OPTION_HEADER, entry.first.c_str(), entry.second.c_str());
  }

  if (file.CURLOpen(ADDON_READ_CHUNKED | ADDON_READ_NO_CACHE | ADDON_READ_AUDIO_VIDEO))
  {
    int returnCode = -1;
    std::string proto = file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_PROTOCOL, "");
    std::string::size_type posResponseCode = proto.find(' ');
    if (posResponseCode != std::string::npos)
      returnCode = atoi(proto.c_str() + (posResponseCode + 1));

    size_t nbRead = ~0UL;

    if (returnCode >= 400)
    {
      LOG::Log(LOGERROR, "Download failed with error %d: %s", returnCode, url.c_str());
    }
    else
    {
      // read the file
      char* buf = (char*)malloc(32 * 1024);
      size_t nbReadOverall = 0;
      bool write_ok = true;
      while ((nbRead = file.Read(buf, 32 * 1024)) > 0 && ~nbRead &&
             (write_ok = write_data(buf, nbRead, lockfreeBuffer)))
        nbReadOverall += nbRead;
      free(buf);

      if (write_ok)
      {
        if (!nbReadOverall)
        {
          LOG::Log(LOGERROR, "Download doesn't provide any data: %s", url.c_str());
          return false;
        }

        double current_download_speed_ = file.GetFileDownloadSpeed();
        //Calculate the new downloadspeed to 1MB
        static const size_t ref_packet = 1024 * 1024;
        if (nbReadOverall >= ref_packet)
          chooser_->set_download_speed(current_download_speed_);
        else
        {
          double ratio = (double)nbReadOverall / ref_packet;
          chooser_->set_download_speed((chooser_->get_download_speed() * (1.0 - ratio)) +
                                       current_download_speed_ * ratio);
        }
        LOG::Log(LOGDEBUG,
                 "Download %s finished, avg speed: %0.2lfbyte/s, current speed: %0.2lfbyte/s",
                 url.c_str(), chooser_->get_download_speed(), current_download_speed_);
        //pass download speed to
      }
      else
        LOG::Log(LOGDEBUG, "Download %s cancelled", url.c_str());
    }
    file.Close();
    return nbRead == 0;
  }
  return false;
}

bool KodiAdaptiveStream::parseIndexRange(adaptive::AdaptiveTree::Representation* rep,
                                         const std::string& buffer)
{
  LOG::Log(LOGDEBUG, "Build segments from SIDX atom...");
  AP4_MemoryByteStream byteStream((const AP4_Byte*)(buffer.data()), buffer.size());

  adaptive::AdaptiveTree::AdaptationSet* adp(
      const_cast<adaptive::AdaptiveTree::AdaptationSet*>(getAdaptationSet()));

  if (rep->containerType_ == adaptive::AdaptiveTree::CONTAINERTYPE_WEBM)
  {
    if (!rep->indexRangeMin_)
      return false;
    WebmReader reader(&byteStream);
    std::vector<WebmReader::CUEPOINT> cuepoints;
    reader.GetCuePoints(cuepoints);

    if (!cuepoints.empty())
    {
      adaptive::AdaptiveTree::Segment seg;

      rep->timescale_ = 1000;
      rep->SetScaling();

      rep->segments_.data.reserve(cuepoints.size());
      adp->segment_durations_.data.reserve(cuepoints.size());

      for (const WebmReader::CUEPOINT& cue : cuepoints)
      {
        seg.startPTS_ = cue.pts;
        seg.range_begin_ = cue.pos_start;
        seg.range_end_ = cue.pos_end;
        rep->segments_.data.push_back(seg);

        if (adp->segment_durations_.data.size() < rep->segments_.data.size())
          adp->segment_durations_.data.push_back(static_cast<const uint32_t>(cue.duration));
      }
      return true;
    }
  }

  if (rep->containerType_ == adaptive::AdaptiveTree::CONTAINERTYPE_MP4)
  {
    if (!rep->indexRangeMin_)
    {
      AP4_File f(byteStream, AP4_DefaultAtomFactory::Instance_, true);
      AP4_Movie* movie = f.GetMovie();
      if (movie == NULL)
      {
        LOG::Log(LOGERROR, "No MOOV in stream!");
        return false;
      }
      rep->flags_ |= adaptive::AdaptiveTree::Representation::INITIALIZATION;
      rep->initialization_.range_begin_ = 0;
      AP4_Position pos;
      byteStream.Tell(pos);
      rep->initialization_.range_end_ = pos - 1;
    }

    adaptive::AdaptiveTree::Segment seg;
    seg.startPTS_ = 0;
    unsigned int numSIDX(1);

    do
    {
      AP4_Atom* atom(NULL);
      if (AP4_FAILED(AP4_DefaultAtomFactory::Instance_.CreateAtomFromStream(byteStream, atom)))
      {
        LOG::Log(LOGERROR, "Unable to create SIDX from IndexRange bytes");
        return false;
      }

      if (atom->GetType() == AP4_ATOM_TYPE_MOOF)
      {
        delete atom;
        break;
      }
      else if (atom->GetType() != AP4_ATOM_TYPE_SIDX)
      {
        delete atom;
        continue;
      }

      AP4_SidxAtom* sidx(AP4_DYNAMIC_CAST(AP4_SidxAtom, atom));
      const AP4_Array<AP4_SidxAtom::Reference>& refs(sidx->GetReferences());
      if (refs[0].m_ReferenceType == 1)
      {
        numSIDX = refs.ItemCount();
        delete atom;
        continue;
      }
      AP4_Position pos;
      byteStream.Tell(pos);
      seg.range_end_ = pos + rep->indexRangeMin_ + sidx->GetFirstOffset() - 1;
      rep->timescale_ = sidx->GetTimeScale();
      rep->SetScaling();

      for (unsigned int i(0); i < refs.ItemCount(); ++i)
      {
        seg.range_begin_ = seg.range_end_ + 1;
        seg.range_end_ = seg.range_begin_ + refs[i].m_ReferencedSize - 1;
        rep->segments_.data.push_back(seg);
        if (adp->segment_durations_.data.size() < rep->segments_.data.size())
          adp->segment_durations_.data.push_back(refs[i].m_SubsegmentDuration);
        seg.startPTS_ += refs[i].m_SubsegmentDuration;
      }
      delete atom;
      --numSIDX;
    } while (numSIDX);
    return true;
  }
  return false;
}

/*******************************************************
|   SampleReader
********************************************************/

class ATTR_DLL_LOCAL SampleReader
{
public:
  virtual ~SampleReader() = default;
  virtual bool Initialize() { return true; };
  virtual bool EOS() const = 0;
  virtual uint64_t DTS() const = 0;
  virtual uint64_t PTS() const = 0;
  virtual uint64_t DTSorPTS() const { return DTS() < PTS() ? DTS() : PTS(); };
  virtual AP4_Result Start(bool& bStarted) = 0;
  virtual AP4_Result ReadSample() = 0;
  virtual void Reset(bool bEOS) = 0;
  virtual bool GetInformation(kodi::addon::InputstreamInfo& info) = 0;
  virtual bool TimeSeek(uint64_t pts, bool preceeding) = 0;
  virtual void SetPTSOffset(uint64_t offset) = 0;
  virtual int64_t GetPTSDiff() const = 0;
  virtual bool GetNextFragmentInfo(uint64_t& ts, uint64_t& dur) = 0;
  virtual uint32_t GetTimeScale() const = 0;
  virtual AP4_UI32 GetStreamId() const = 0;
  virtual AP4_Size GetSampleDataSize() const = 0;
  virtual const AP4_Byte* GetSampleData() const = 0;
  virtual uint64_t GetDuration() const = 0;
  virtual bool IsEncrypted() const = 0;
  virtual void AddStreamType(INPUTSTREAM_TYPE type, uint32_t sid){};
  virtual void SetStreamType(INPUTSTREAM_TYPE type, uint32_t sid){};
  virtual bool RemoveStreamType(INPUTSTREAM_TYPE type) { return true; };
  virtual bool IsStarted() const = 0;
};

/*******************************************************
|   DummySampleReader
********************************************************/

class ATTR_DLL_LOCAL DummyReader : public SampleReader
{
public:
  virtual ~DummyReader() = default;
  bool EOS() const override { return false; }
  uint64_t DTS() const override { return STREAM_NOPTS_VALUE; }
  uint64_t PTS() const override { return STREAM_NOPTS_VALUE; }
  AP4_Result Start(bool& bStarted) override { return AP4_SUCCESS; }
  AP4_Result ReadSample() override { return AP4_SUCCESS; }
  void Reset(bool bEOS) override {}
  bool GetInformation(kodi::addon::InputstreamInfo& info) override { return false; }
  bool TimeSeek(uint64_t pts, bool preceeding) override { return false; }
  void SetPTSOffset(uint64_t offset) override {}
  int64_t GetPTSDiff() const override { return 0; }
  bool GetNextFragmentInfo(uint64_t& ts, uint64_t& dur) override { return false; }
  uint32_t GetTimeScale() const override { return 1; }
  AP4_UI32 GetStreamId() const override { return 0; }
  AP4_Size GetSampleDataSize() const override { return 0; }
  const AP4_Byte* GetSampleData() const override { return nullptr; }
  uint64_t GetDuration() const override { return 0; }
  bool IsEncrypted() const override { return false; }
  void AddStreamType(INPUTSTREAM_TYPE type, uint32_t sid) override{};
  void SetStreamType(INPUTSTREAM_TYPE type, uint32_t sid) override{};
  bool RemoveStreamType(INPUTSTREAM_TYPE type) override { return true; };
  bool IsStarted() const override { return true; }
} DummyReader;

/*******************************************************
|   FragmentedSampleReader
********************************************************/
class ATTR_DLL_LOCAL FragmentedSampleReader : public SampleReader, public AP4_LinearReader
{
public:
  FragmentedSampleReader(AP4_ByteStream* input,
                         AP4_Movie* movie,
                         AP4_Track* track,
                         AP4_UI32 streamId,
                         Adaptive_CencSingleSampleDecrypter* ssd,
                         const SSD::SSD_DECRYPTER::SSD_CAPS& dcaps)
    : AP4_LinearReader(*movie, input),
      m_track(track),
      m_streamId(streamId),
      m_sampleDescIndex(1),
      m_bSampleDescChanged(false),
      m_decrypterCaps(dcaps),
      m_failCount(0),
      m_eos(false),
      m_started(false),
      m_dts(0),
      m_pts(0),
      m_ptsDiff(0),
      m_ptsOffs(~0ULL),
      m_codecHandler(0),
      m_defaultKey(0),
      m_protectedDesc(0),
      m_singleSampleDecryptor(ssd),
      m_decrypter(0),
      m_nextDuration(0),
      m_nextTimestamp(0)
  {
    EnableTrack(m_track->GetId());

    AP4_SampleDescription* desc(m_track->GetSampleDescription(0));
    if (desc->GetType() == AP4_SampleDescription::TYPE_PROTECTED)
    {
      m_protectedDesc = static_cast<AP4_ProtectedSampleDescription*>(desc);

      AP4_ContainerAtom* schi;
      if (m_protectedDesc->GetSchemeInfo() &&
          (schi = m_protectedDesc->GetSchemeInfo()->GetSchiAtom()))
      {
        AP4_TencAtom* tenc(AP4_DYNAMIC_CAST(AP4_TencAtom, schi->GetChild(AP4_ATOM_TYPE_TENC, 0)));
        if (tenc)
          m_defaultKey = tenc->GetDefaultKid();
        else
        {
          AP4_PiffTrackEncryptionAtom* piff(AP4_DYNAMIC_CAST(
              AP4_PiffTrackEncryptionAtom, schi->GetChild(AP4_UUID_PIFF_TRACK_ENCRYPTION_ATOM, 0)));
          if (piff)
            m_defaultKey = piff->GetDefaultKid();
        }
      }
    }
    if (m_singleSampleDecryptor)
      m_poolId = m_singleSampleDecryptor->AddPool();

    m_timeBaseExt = STREAM_TIME_BASE;
    m_timeBaseInt = m_track->GetMediaTimeScale();

    while (m_timeBaseExt > 1)
      if ((m_timeBaseInt / 10) * 10 == m_timeBaseInt)
      {
        m_timeBaseExt /= 10;
        m_timeBaseInt /= 10;
      }
      else
        break;

    //We need this to fill extradata
    UpdateSampleDescription();
  }

  ~FragmentedSampleReader()
  {
    if (m_singleSampleDecryptor)
      m_singleSampleDecryptor->RemovePool(m_poolId);
    delete m_decrypter;
    delete m_codecHandler;
  }

  AP4_Result Start(bool& bStarted) override
  {
    bStarted = false;
    if (m_started)
      return AP4_SUCCESS;
    m_started = true;
    bStarted = true;
    return ReadSample();
  }

  AP4_Result ReadSample() override
  {
    AP4_Result result;
    if (!m_codecHandler || !m_codecHandler->ReadNextSample(m_sample, m_sampleData))
    {
      bool useDecryptingDecoder =
          m_protectedDesc &&
          (m_decrypterCaps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH) != 0;
      bool decrypterPresent(m_decrypter != nullptr);
      if (AP4_FAILED(result = ReadNextSample(m_track->GetId(), m_sample,
                                             (m_decrypter || useDecryptingDecoder) ? m_encrypted
                                                                                   : m_sampleData)))
      {
        if (result == AP4_ERROR_EOS)
        {
          auto adByteStream = dynamic_cast<AdaptiveByteStream*>(m_FragmentStream);
          if (!adByteStream)
          {
            LOG::LogF(LOGERROR, "Fragment stream cannot be casted to AdaptiveByteStream");
            m_eos = true;
          }
          else
          {
            if (adByteStream->waitingForSegment())
              m_sampleData.SetDataSize(0);
            else
              m_eos = true;
          }
        }
        return result;
      }

      //AP4_AvcSequenceParameterSet sps;
      //AP4_AvcFrameParser::ParseFrameForSPS(m_sampleData.GetData(), m_sampleData.GetDataSize(), 4, sps);

      //Protection could have changed in ProcessMoof
      if (!decrypterPresent && m_decrypter != nullptr && !useDecryptingDecoder)
        m_encrypted.SetData(m_sampleData.GetData(), m_sampleData.GetDataSize());
      else if (decrypterPresent && m_decrypter == nullptr && !useDecryptingDecoder)
        m_sampleData.SetData(m_encrypted.GetData(), m_encrypted.GetDataSize());

      if (m_decrypter)
      {
        // Make sure that the decrypter is NOT allocating memory!
        // If decrypter and addon are compiled with different DEBUG / RELEASE
        // options freeing HEAP memory will fail.
        m_sampleData.Reserve(m_encrypted.GetDataSize() + 4096);
        if (AP4_FAILED(
                result = m_decrypter->DecryptSampleData(m_poolId, m_encrypted, m_sampleData, NULL)))
        {
          LOG::Log(LOGERROR, "Decrypt Sample returns failure!");
          if (++m_failCount > 50)
          {
            Reset(true);
            return result;
          }
          else
            m_sampleData.SetDataSize(0);
        }
        else
          m_failCount = 0;
      }
      else if (useDecryptingDecoder)
      {
        m_sampleData.Reserve(m_encrypted.GetDataSize() + 1024);
        m_singleSampleDecryptor->DecryptSampleData(m_poolId, m_encrypted, m_sampleData, nullptr, 0,
                                                   nullptr, nullptr);
      }

      if (m_codecHandler->Transform(m_sample.GetDts(), m_sample.GetDuration(), m_sampleData,
                                    m_track->GetMediaTimeScale()))
        m_codecHandler->ReadNextSample(m_sample, m_sampleData);
    }

    m_dts = (m_sample.GetDts() * m_timeBaseExt) / m_timeBaseInt;
    m_pts = (m_sample.GetCts() * m_timeBaseExt) / m_timeBaseInt;

    m_codecHandler->UpdatePPSId(m_sampleData);

    return AP4_SUCCESS;
  };

  void Reset(bool bEOS) override
  {
    AP4_LinearReader::Reset();
    m_eos = bEOS;
    if (m_codecHandler)
      m_codecHandler->Reset();
  }

  bool EOS() const override { return m_eos; };
  bool IsStarted() const override { return m_started; };
  uint64_t DTS() const override { return m_dts; };
  uint64_t PTS() const override { return m_pts; };
  AP4_UI32 GetStreamId() const override { return m_streamId; };
  AP4_Size GetSampleDataSize() const override { return m_sampleData.GetDataSize(); };
  const AP4_Byte* GetSampleData() const override { return m_sampleData.GetData(); };
  uint64_t GetDuration() const override
  {
    return (m_sample.GetDuration() * m_timeBaseExt) / m_timeBaseInt;
  };
  bool IsEncrypted() const override
  {
    return (m_decrypterCaps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH) != 0 &&
           m_decrypter != nullptr;
  };
  bool GetInformation(kodi::addon::InputstreamInfo& info) override
  {
    if (!m_codecHandler)
      return false;

    bool edchanged(false);
    if (m_bSampleDescChanged && m_codecHandler->m_extraData.GetDataSize() &&
        !info.CompareExtraData(m_codecHandler->m_extraData.GetData(),
                               m_codecHandler->m_extraData.GetDataSize()))
    {
      info.SetExtraData(m_codecHandler->m_extraData.GetData(),
                        m_codecHandler->m_extraData.GetDataSize());
      edchanged = true;
    }

    AP4_SampleDescription* desc(m_track->GetSampleDescription(0));
    if (desc->GetType() == AP4_SampleDescription::TYPE_MPEG)
    {
      switch (static_cast<AP4_MpegSampleDescription*>(desc)->GetObjectTypeId())
      {
        case AP4_OTI_MPEG4_AUDIO:
        case AP4_OTI_MPEG2_AAC_AUDIO_MAIN:
        case AP4_OTI_MPEG2_AAC_AUDIO_LC:
        case AP4_OTI_MPEG2_AAC_AUDIO_SSRP:
          info.SetCodecName("aac");
          break;
        case AP4_OTI_DTS_AUDIO:
        case AP4_OTI_DTS_HIRES_AUDIO:
        case AP4_OTI_DTS_MASTER_AUDIO:
        case AP4_OTI_DTS_EXPRESS_AUDIO:
          info.SetCodecName("dca");
          break;
        case AP4_OTI_AC3_AUDIO:
          info.SetCodecName("ac3");
          break;
        case AP4_OTI_EAC3_AUDIO:
          info.SetCodecName("eac3");
          break;
      }
    }

    m_bSampleDescChanged = false;

    if (m_codecHandler->GetInformation(info))
      return true;

    return edchanged;
  }

  bool TimeSeek(uint64_t pts, bool preceeding) override
  {
    AP4_Ordinal sampleIndex;
    AP4_UI64 seekPos(static_cast<AP4_UI64>((pts * m_timeBaseInt) / m_timeBaseExt));
    if (AP4_SUCCEEDED(SeekSample(m_track->GetId(), seekPos, sampleIndex, preceeding)))
    {
      if (m_decrypter)
        m_decrypter->SetSampleIndex(sampleIndex);
      if (m_codecHandler)
        m_codecHandler->TimeSeek(seekPos);
      m_started = true;
      return AP4_SUCCEEDED(ReadSample());
    }
    return false;
  };

  void SetPTSOffset(uint64_t offset) override
  {
    FindTracker(m_track->GetId())->m_NextDts = (offset * m_timeBaseInt) / m_timeBaseExt;
    m_ptsOffs = offset;
    if (m_codecHandler)
      m_codecHandler->SetPTSOffset((offset * m_timeBaseInt) / m_timeBaseExt);
  };

  int64_t GetPTSDiff() const override { return m_ptsDiff; }

  bool GetNextFragmentInfo(uint64_t& ts, uint64_t& dur) override
  {
    if (m_nextDuration)
    {
      dur = m_nextDuration;
      ts = m_nextTimestamp;
    }
    else
    {
      dur = dynamic_cast<AP4_FragmentSampleTable*>(FindTracker(m_track->GetId())->m_SampleTable)
                ->GetDuration();
      ts = 0;
    }
    return true;
  };
  uint32_t GetTimeScale() const override { return m_track->GetMediaTimeScale(); };

public:
  static const AP4_UI32 TRACKID_UNKNOWN = -1;

protected:
  AP4_Result ProcessMoof(AP4_ContainerAtom* moof,
                         AP4_Position moof_offset,
                         AP4_Position mdat_payload_offset,
                         AP4_UI64 mdat_payload_size) override
  {
    // For prefixed initialization (usually ISM) we don't yet know the
    // proper track id, let's find it now
    if (m_track->GetId() == TRACKID_UNKNOWN)
    {
      AP4_MovieFragment* fragment = new AP4_MovieFragment(
          AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof->Clone()));
      AP4_Array<AP4_UI32> ids;
      fragment->GetTrackIds(ids);
      if (ids.ItemCount() == 1)
      {
        m_track->SetId(ids[0]);
        delete fragment;
      }
      else
      {
        delete fragment;
        return AP4_ERROR_NO_SUCH_ITEM;
      }
    }
    
    AP4_Result result;

    if (AP4_SUCCEEDED((result = AP4_LinearReader::ProcessMoof(
                           moof, moof_offset, mdat_payload_offset, mdat_payload_size))))
    {
      AP4_ContainerAtom* traf =
          AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof->GetChild(AP4_ATOM_TYPE_TRAF, 0));

      //For ISM Livestreams we have an UUID atom with one / more following fragment durations
      m_nextDuration = m_nextTimestamp = 0;
      AP4_Atom* atom;
      unsigned int atom_pos(0);
      const uint8_t uuid[16] = {0xd4, 0x80, 0x7e, 0xf2, 0xca, 0x39, 0x46, 0x95,
                                0x8e, 0x54, 0x26, 0xcb, 0x9e, 0x46, 0xa7, 0x9f};
      while ((atom = traf->GetChild(AP4_ATOM_TYPE_UUID, atom_pos++)) != nullptr)
      {
        AP4_UuidAtom* uuid_atom(AP4_DYNAMIC_CAST(AP4_UuidAtom, atom));
        if (memcmp(uuid_atom->GetUuid(), uuid, 16) == 0)
        {
          //verison(8) + flags(24) + numpairs(8) + pairs(ts(64)/dur(64))*numpairs
          const AP4_DataBuffer& buf(AP4_DYNAMIC_CAST(AP4_UnknownUuidAtom, uuid_atom)->GetData());
          if (buf.GetDataSize() >= 21)
          {
            const uint8_t* data(buf.GetData());
            m_nextTimestamp = AP4_BytesToUInt64BE(data + 5);
            m_nextDuration = AP4_BytesToUInt64BE(data + 13);
          }
          break;
        }
      }

      //Check if the sample table description has changed
      AP4_TfhdAtom* tfhd = AP4_DYNAMIC_CAST(AP4_TfhdAtom, traf->GetChild(AP4_ATOM_TYPE_TFHD, 0));
      if ((tfhd && tfhd->GetSampleDescriptionIndex() != m_sampleDescIndex) ||
          (!tfhd && (m_sampleDescIndex = 1)))
      {
        m_sampleDescIndex = tfhd->GetSampleDescriptionIndex();
        UpdateSampleDescription();
      }

      //Correct PTS
      AP4_Sample sample;
      if (~m_ptsOffs)
      {
        if (AP4_SUCCEEDED(GetSample(m_track->GetId(), sample, 0)))
        {
          m_pts = m_dts = (sample.GetCts() * m_timeBaseExt) / m_timeBaseInt;
          m_ptsDiff = m_pts - m_ptsOffs;
        }
        m_ptsOffs = ~0ULL;
      }

      if (m_protectedDesc)
      {
        //Setup the decryption
        AP4_CencSampleInfoTable* sample_table;
        AP4_UI32 algorithm_id = 0;

        delete m_decrypter;
        m_decrypter = 0;

        AP4_ContainerAtom* traf =
            AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof->GetChild(AP4_ATOM_TYPE_TRAF, 0));

        if (!m_protectedDesc || !traf)
          return AP4_ERROR_INVALID_FORMAT;

        bool reset_iv(false);
        if (AP4_FAILED(result = AP4_CencSampleInfoTable::Create(m_protectedDesc, traf, algorithm_id,
                                                                reset_iv, *m_FragmentStream, moof_offset,
                                                                sample_table)))
          // we assume unencrypted fragment here
          goto SUCCESS;

        if (AP4_FAILED(result =
                           AP4_CencSampleDecrypter::Create(sample_table, algorithm_id, 0, 0, 0, reset_iv,
                                                           m_singleSampleDecryptor, m_decrypter)))
          return result;

        // Inform decrypter of pattern decryption (CBCS)
        if (m_protectedDesc->GetSchemeType() == AP4_PROTECTION_SCHEME_TYPE_CENC)
        {
          m_singleSampleDecryptor->SetEncryptionScheme(ENCRYPTION_SCHEME::CENC);
          m_singleSampleDecryptor->SetCrypto(0, 0);
        }
        else if (m_protectedDesc->GetSchemeType() == AP4_PROTECTION_SCHEME_TYPE_CBCS)
        {
          m_singleSampleDecryptor->SetEncryptionScheme(ENCRYPTION_SCHEME::CBCS);
          m_singleSampleDecryptor->SetCrypto(sample_table->GetCryptByteBlock(),
                                             sample_table->GetSkipByteBlock());
        }
      }
    }
  SUCCESS:
    if (m_singleSampleDecryptor && m_codecHandler)
      m_singleSampleDecryptor->SetFragmentInfo(m_poolId, m_defaultKey,
                                               m_codecHandler->m_naluLengthSize,
                                               m_codecHandler->m_extraData, m_decrypterCaps.flags);

    return AP4_SUCCESS;
  }

private:
  void UpdateSampleDescription()
  {
    if (m_codecHandler)
      delete m_codecHandler;
    m_codecHandler = 0;
    m_bSampleDescChanged = true;

    AP4_SampleDescription* desc(m_track->GetSampleDescription(m_sampleDescIndex - 1));
    if (desc->GetType() == AP4_SampleDescription::TYPE_PROTECTED)
    {
      m_protectedDesc = static_cast<AP4_ProtectedSampleDescription*>(desc);
      desc = m_protectedDesc->GetOriginalSampleDescription();
    }
    LOG::Log(LOGDEBUG, "UpdateSampleDescription: codec %d", desc->GetFormat());
    switch (desc->GetFormat())
    {
      case AP4_SAMPLE_FORMAT_AVC1:
      case AP4_SAMPLE_FORMAT_AVC2:
      case AP4_SAMPLE_FORMAT_AVC3:
      case AP4_SAMPLE_FORMAT_AVC4:
        m_codecHandler = new AVCCodecHandler(desc);
        break;
      case AP4_SAMPLE_FORMAT_HEV1:
      case AP4_SAMPLE_FORMAT_HVC1:
      case AP4_SAMPLE_FORMAT_DVHE:
      case AP4_SAMPLE_FORMAT_DVH1:
        m_codecHandler = new HEVCCodecHandler(desc);
        break;
      case AP4_SAMPLE_FORMAT_MP4A:
        m_codecHandler = new MPEGCodecHandler(desc);
        break;
      case AP4_SAMPLE_FORMAT_STPP:
        m_codecHandler = new TTMLCodecHandler(desc);
        break;
      case AP4_SAMPLE_FORMAT_WVTT:
        m_codecHandler = new WebVTTCodecHandler(desc, false);
        break;
      case AP4_SAMPLE_FORMAT_VP9:
        m_codecHandler = new VP9CodecHandler(desc);
        break;
      default:
        m_codecHandler = new CodecHandler(desc);
        break;
    }

    if ((m_decrypterCaps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_ANNEXB_REQUIRED) != 0)
      m_codecHandler->ExtraDataToAnnexB();
  }

private:
  AP4_Track* m_track;
  AP4_UI32 m_streamId;
  AP4_UI32 m_sampleDescIndex;
  bool m_bSampleDescChanged;
  SSD::SSD_DECRYPTER::SSD_CAPS m_decrypterCaps;
  unsigned int m_failCount;
  AP4_UI32 m_poolId;

  bool m_eos, m_started;
  int64_t m_dts, m_pts, m_ptsDiff;
  AP4_UI64 m_ptsOffs;

  uint64_t m_timeBaseExt, m_timeBaseInt;

  AP4_Sample m_sample;
  AP4_DataBuffer m_encrypted, m_sampleData;

  CodecHandler* m_codecHandler;
  const AP4_UI08* m_defaultKey;

  AP4_ProtectedSampleDescription* m_protectedDesc;
  Adaptive_CencSingleSampleDecrypter* m_singleSampleDecryptor;
  AP4_CencSampleDecrypter* m_decrypter;
  uint64_t m_nextDuration, m_nextTimestamp;
};

/*******************************************************
|   SubtitleSampleReader
********************************************************/

class ATTR_DLL_LOCAL SubtitleSampleReader : public SampleReader
{
public:
  SubtitleSampleReader(const std::string& url,
                       AP4_UI32 streamId,
                       const std::string& codecInternalName)
    : m_pts(0), m_streamId(streamId), m_eos(false)
  {
    // open the file
    kodi::vfs::CFile file;
    if (!file.CURLCreate(url))
      return;

    file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "seekable", "0");
    file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "acceptencoding", "gzip");
    file.CURLOpen(ADDON_READ_CHUNKED | ADDON_READ_NO_CACHE);

    AP4_DataBuffer result;

    // read the file
    static const unsigned int CHUNKSIZE = 16384;
    AP4_Byte buf[CHUNKSIZE];
    size_t nbRead;
    while ((nbRead = file.Read(buf, CHUNKSIZE)) > 0 && ~nbRead)
      result.AppendData(buf, nbRead);
    file.Close();

    if (codecInternalName == "wvtt")
      m_codecHandler = new WebVTTCodecHandler(nullptr, true);
    else
      m_codecHandler = new TTMLCodecHandler(nullptr);

    m_codecHandler->Transform(0, 0, result, 1000);
  };

  SubtitleSampleReader(Session::STREAM* stream,
                       AP4_UI32 streamId,
                       const std::string& codecInternalName)
    : m_pts(0),
      m_streamId(streamId),
      m_eos(false),
      m_adByteStream(stream->GetAdByteStream()),
      m_kodiAdStream(&stream->m_kodiAdStream)
  {
    if (codecInternalName == "wvtt")
      m_codecHandler = new WebVTTCodecHandler(nullptr, false);
    else
      m_codecHandler = new TTMLCodecHandler(nullptr);
  }

  bool IsStarted() const override { return true; };
  bool EOS() const override { return m_eos; };
  uint64_t DTS() const override { return m_pts; };
  uint64_t PTS() const override { return m_pts; };
  AP4_Result Start(bool& bStarted) override
  {
    m_eos = false;
    return AP4_SUCCESS;
  };
  AP4_Result ReadSample() override
  {
    if (m_codecHandler->ReadNextSample(m_sample,
                                       m_sampleData)) // Read the sample data from a file url
    {
      m_pts = m_sample.GetCts() * 1000;
      return AP4_SUCCESS;
    }
    else if (m_adByteStream)  // Read the sample data from a segment file stream (e.g. HLS)
    {
      // Get the next segment
      if (m_kodiAdStream && m_kodiAdStream->ensureSegment())
      {
        size_t segSize;
        if (m_kodiAdStream->retrieveCurrentSegmentBufferSize(segSize))
        {
          AP4_DataBuffer segData;
          while (segSize > 0)
          {
            AP4_Size readSize = m_segmentChunkSize;
            if (segSize < static_cast<size_t>(m_segmentChunkSize))
              readSize = static_cast<AP4_Size>(segSize);

            AP4_Byte* buf = new AP4_Byte[readSize];
            segSize -= readSize;
            if (AP4_SUCCEEDED(m_adByteStream->Read(buf, readSize)))
            {
              segData.AppendData(buf, readSize);
              delete[] buf;
            }
            else
            {
              delete[] buf;
              break;
            }
          }
          auto rep = m_kodiAdStream->getRepresentation();
          if (rep)
          {
            auto currentSegment = rep->current_segment_;
            if (currentSegment)
            {
              m_codecHandler->Transform(currentSegment->startPTS_, currentSegment->m_duration,
                                        segData, 1000);
              if (m_codecHandler->ReadNextSample(m_sample, m_sampleData))
              {
                m_pts = m_sample.GetCts();
                m_ptsDiff = m_pts - m_ptsOffset;
                return AP4_SUCCESS;
              }
            }
            else
              LOG::LogF(LOGERROR, "Failed to get current segment of subtitle stream");
          }
          else
            LOG::LogF(LOGERROR, "Failed to get Representation of subtitle stream");
        }
        else
        {
          LOG::LogF(LOGERROR, "Failed to get subtitle segment buffer size");
        }
      }
    }
    m_eos = true;
    return AP4_ERROR_EOS;
  }
  void Reset(bool bEOS) override
  {
    if (m_adByteStream || bEOS)
      m_codecHandler->Reset();
  };
  bool GetInformation(kodi::addon::InputstreamInfo& info) override
  {
    if (m_codecHandler->m_extraData.GetDataSize() &&
        !info.CompareExtraData(m_codecHandler->m_extraData.GetData(),
                               m_codecHandler->m_extraData.GetDataSize()))
    {
      info.SetExtraData(m_codecHandler->m_extraData.GetData(),
                        m_codecHandler->m_extraData.GetDataSize());
      return true;
    }
    return false;
  };
  bool TimeSeek(uint64_t pts, bool preceeding) override
  {
    if (dynamic_cast<WebVTTCodecHandler*>(m_codecHandler))
    {
      m_pts = pts;
      return true;
    }
    else
    {
      if (m_codecHandler->TimeSeek(pts / 1000))
        return AP4_SUCCEEDED(ReadSample());
      return false;
    }
  };
  void SetPTSOffset(uint64_t offset) override { m_ptsOffset = offset; };
  int64_t GetPTSDiff() const override { return m_ptsDiff; }
  bool GetNextFragmentInfo(uint64_t& ts, uint64_t& dur) override { return false; };
  uint32_t GetTimeScale() const override { return 1000; };
  AP4_UI32 GetStreamId() const override { return m_streamId; };
  AP4_Size GetSampleDataSize() const override { return m_sampleData.GetDataSize(); };
  const AP4_Byte* GetSampleData() const override { return m_sampleData.GetData(); };
  uint64_t GetDuration() const override { return m_sample.GetDuration() * 1000; };
  bool IsEncrypted() const override { return false; };

private:
  uint64_t m_pts, m_ptsOffset = 0, m_ptsDiff = 0;
  AP4_UI32 m_streamId;
  bool m_eos;

  CodecHandler* m_codecHandler;

  AP4_Sample m_sample;
  AP4_DataBuffer m_sampleData;
  AdaptiveByteStream* m_adByteStream{nullptr};
  KodiAdaptiveStream* m_kodiAdStream{nullptr};
  const AP4_Size m_segmentChunkSize = 16384; // 16kb
};

/*******************************************************
|   TSSampleReader
********************************************************/
class ATTR_DLL_LOCAL TSSampleReader : public SampleReader, public TSReader
{
public:
  TSSampleReader(AP4_ByteStream* input,
                 INPUTSTREAM_TYPE type,
                 AP4_UI32 streamId,
                 uint32_t requiredMask)
    : TSReader(input, requiredMask),
      m_adByteStream(dynamic_cast<AdaptiveByteStream*>(input)),
      m_typeMask(1 << type)
  {
    m_typeMap[type] = m_typeMap[INPUTSTREAM_TYPE_NONE] = streamId;
  };

  bool Initialize() override { return TSReader::Initialize(); }

  void AddStreamType(INPUTSTREAM_TYPE type, uint32_t sid) override
  {
    m_typeMap[type] = sid;
    m_typeMask |= (1 << type);
    if (m_started)
      StartStreaming(m_typeMask);
  };

  void SetStreamType(INPUTSTREAM_TYPE type, uint32_t sid) override
  {
    m_typeMap[type] = sid;
    m_typeMask = (1 << type);
  };

  bool RemoveStreamType(INPUTSTREAM_TYPE type) override
  {
    m_typeMask &= ~(1 << type);
    StartStreaming(m_typeMask);
    return m_typeMask == 0;
  };

  bool IsStarted() const override { return m_started; }
  bool EOS() const override { return m_eos; }
  uint64_t DTS() const override { return m_dts; }
  uint64_t PTS() const override { return m_pts; }
  AP4_Result Start(bool& bStarted) override
  {
    bStarted = false;
    if (m_started)
      return AP4_SUCCESS;

    if (!StartStreaming(m_typeMask))
    {
      m_eos = true;
      return AP4_ERROR_CANNOT_OPEN_FILE;
    }

    m_started = bStarted = true;
    return ReadSample();
  }

  AP4_Result ReadSample() override
  {
    if (ReadPacket())
    {
      m_dts = (GetDts() == PTS_UNSET) ? STREAM_NOPTS_VALUE : (GetDts() * 100) / 9;
      m_pts = (GetPts() == PTS_UNSET) ? STREAM_NOPTS_VALUE : (GetPts() * 100) / 9;

      if (~m_ptsOffs)
      {
        m_ptsDiff = m_pts - m_ptsOffs;
        m_ptsOffs = ~0ULL;
      }
      return AP4_SUCCESS;
    }
    if (!m_adByteStream || !m_adByteStream->waitingForSegment())
      m_eos = true;
    return AP4_ERROR_EOS;
  }

  void Reset(bool bEOS) override
  {
    TSReader::Reset();
    m_eos = bEOS;
  }

  bool GetInformation(kodi::addon::InputstreamInfo& info) override
  {
    return TSReader::GetInformation(info);
  }

  bool TimeSeek(uint64_t pts, bool preceeding) override
  {
    if (!StartStreaming(m_typeMask))
      return false;

    AP4_UI64 seekPos((pts * 9) / 100);
    if (TSReader::SeekTime(seekPos, preceeding))
    {
      m_started = true;
      return AP4_SUCCEEDED(ReadSample());
    }
    return AP4_ERROR_EOS;
  }

  void SetPTSOffset(uint64_t offset) override { m_ptsOffs = offset; }

  int64_t GetPTSDiff() const override { return m_ptsDiff; }

  bool GetNextFragmentInfo(uint64_t& ts, uint64_t& dur) override { return false; }
  uint32_t GetTimeScale() const override { return 90000; }
  AP4_UI32 GetStreamId() const override { return m_typeMap[GetStreamType()]; }
  AP4_Size GetSampleDataSize() const override { return GetPacketSize(); }
  const AP4_Byte* GetSampleData() const override { return GetPacketData(); }
  uint64_t GetDuration() const override { return (TSReader::GetDuration() * 100) / 9; }
  bool IsEncrypted() const override { return false; };

private:
  uint32_t m_typeMask; //Bit representation of INPUTSTREAM_TYPES
  uint32_t m_typeMap[16];
  bool m_eos = false;
  bool m_started = false;

  uint64_t m_pts = 0;
  uint64_t m_dts = 0;
  int64_t m_ptsDiff = 0;
  uint64_t m_ptsOffs = ~0ULL;
  AdaptiveByteStream* m_adByteStream;
};

/*******************************************************
|   ADTSSampleReader
********************************************************/
class ATTR_DLL_LOCAL ADTSSampleReader : public SampleReader, public ADTSReader
{
public:
  ADTSSampleReader(AP4_ByteStream* input, AP4_UI32 streamId)
    : ADTSReader(input),
      m_streamId(streamId),
      m_adByteStream(dynamic_cast<AdaptiveByteStream*>(input)){};

  bool IsStarted() const override { return m_started; }
  bool EOS() const override { return m_eos; }
  uint64_t DTS() const override { return m_pts; }
  uint64_t PTS() const override { return m_pts; }
  AP4_Result Start(bool& bStarted) override
  {
    bStarted = false;
    if (m_started)
      return AP4_SUCCESS;

    m_started = bStarted = true;
    return ReadSample();
  }

  AP4_Result ReadSample() override
  {
    if (ReadPacket())
    {
      m_pts = (GetPts() == PTS_UNSET) ? STREAM_NOPTS_VALUE : (GetPts() * 100) / 9;

      if (~m_ptsOffs)
      {
        m_ptsDiff = m_pts - m_ptsOffs;
        m_ptsOffs = ~0ULL;
      }
      return AP4_SUCCESS;
    }
    if (!m_adByteStream || !m_adByteStream->waitingForSegment())
      m_eos = true;
    return AP4_ERROR_EOS;
  }

  void Reset(bool bEOS) override
  {
    ADTSReader::Reset();
    m_eos = bEOS;
  }

  bool GetInformation(kodi::addon::InputstreamInfo& info) override
  {
    return ADTSReader::GetInformation(info);
  }

  bool TimeSeek(uint64_t pts, bool preceeding) override
  {
    AP4_UI64 seekPos((pts * 9) / 100);
    if (ADTSReader::SeekTime(seekPos, preceeding))
    {
      m_started = true;
      return AP4_SUCCEEDED(ReadSample());
    }
    return AP4_ERROR_EOS;
  }

  void SetPTSOffset(uint64_t offset) override { m_ptsOffs = offset; }

  int64_t GetPTSDiff() const override { return m_ptsDiff; }

  bool GetNextFragmentInfo(uint64_t& ts, uint64_t& dur) override { return false; }
  uint32_t GetTimeScale() const override { return 90000; }
  AP4_UI32 GetStreamId() const override { return m_streamId; }
  AP4_Size GetSampleDataSize() const override { return GetPacketSize(); }
  const AP4_Byte* GetSampleData() const override { return GetPacketData(); }
  uint64_t GetDuration() const override { return (ADTSReader::GetDuration() * 100) / 9; }
  bool IsEncrypted() const override { return false; };

private:
  bool m_eos = false;
  bool m_started = false;
  AP4_UI32 m_streamId = 0;
  uint64_t m_pts = 0;
  int64_t m_ptsDiff = 0;
  uint64_t m_ptsOffs = ~0ULL;
  AdaptiveByteStream* m_adByteStream;
};


/*******************************************************
|   WebmSampleReader
********************************************************/
class ATTR_DLL_LOCAL WebmSampleReader : public SampleReader, public WebmReader
{
public:
  WebmSampleReader(AP4_ByteStream* input, AP4_UI32 streamId)
    : WebmReader(input),
      m_streamId(streamId),
      m_adByteStream(dynamic_cast<AdaptiveByteStream*>(input)){};

  bool IsStarted() const override { return m_started; }
  bool EOS() const override { return m_eos; }
  uint64_t DTS() const override { return m_dts; }
  uint64_t PTS() const override { return m_pts; }

  bool Initialize() override
  {
    m_adByteStream->FixateInitialization(true);
    bool ret = WebmReader::Initialize();
    WebmReader::Reset();
    m_adByteStream->FixateInitialization(false);
    m_adByteStream->SetSegmentFileOffset(GetCueOffset());
    return ret;
  }

  AP4_Result Start(bool& bStarted) override
  {
    bStarted = false;
    if (m_started)
      return AP4_SUCCESS;
    m_started = bStarted = true;
    return ReadSample();
  }

  AP4_Result ReadSample() override
  {
    if (ReadPacket())
    {
      m_dts = GetDts() * 1000;
      m_pts = GetPts() * 1000;

      if (~m_ptsOffs)
      {
        m_ptsDiff = m_pts - m_ptsOffs;
        m_ptsOffs = ~0ULL;
      }
      return AP4_SUCCESS;
    }
    if (!m_adByteStream || !m_adByteStream->waitingForSegment())
      m_eos = true;
    return AP4_ERROR_EOS;
  }

  void Reset(bool bEOS) override
  {
    WebmReader::Reset();
    m_eos = bEOS;
  }

  bool GetInformation(kodi::addon::InputstreamInfo& info) override
  {
    return WebmReader::GetInformation(info);
  }

  bool TimeSeek(uint64_t pts, bool preceeding) override
  {
    AP4_UI64 seekPos((pts * 9) / 100);
    if (WebmReader::SeekTime(seekPos, preceeding))
    {
      m_started = true;
      return AP4_SUCCEEDED(ReadSample());
    }
    return AP4_ERROR_EOS;
  }

  void SetPTSOffset(uint64_t offset) override { m_ptsOffs = offset; }

  int64_t GetPTSDiff() const override { return m_ptsDiff; }

  bool GetNextFragmentInfo(uint64_t& ts, uint64_t& dur) override { return false; }
  uint32_t GetTimeScale() const override { return 1000; }
  AP4_UI32 GetStreamId() const override { return m_streamId; }
  AP4_Size GetSampleDataSize() const override { return GetPacketSize(); }
  const AP4_Byte* GetSampleData() const override { return GetPacketData(); }
  uint64_t GetDuration() const override { return WebmReader::GetDuration() * 1000; }
  bool IsEncrypted() const override { return false; };

private:
  AP4_UI32 m_streamId = 0;
  bool m_eos = false;
  bool m_started = false;

  uint64_t m_pts = 0;
  uint64_t m_dts = 0;
  int64_t m_ptsDiff = 0;
  uint64_t m_ptsOffs = ~0ULL;
  AdaptiveByteStream* m_adByteStream;
};

/*******************************************************
Main class Session
********************************************************/

void Session::STREAM::disable()
{
  if (enabled)
  {
    m_kodiAdStream.stop();
    reset();
    enabled = encrypted = false;
  }
}

void Session::STREAM::reset()
{
  if (enabled)
  {
    m_streamReader.reset();
    m_streamFile.reset();
    m_adByteStream.reset();
    mainId_ = 0;
  }
}

Session::Session(const PROPERTIES::KodiProperties& kodiProps,
                 const std::string& url,
                 const std::map<std::string, std::string>& mediaHeaders,
                 const std::string& profilePath)
  : m_kodiProps(kodiProps),
    manifestURL_(url),
    media_headers_(mediaHeaders),
    profile_path_(profilePath)
{
  switch (kodiProps.m_manifestType)
  {
    case PROPERTIES::ManifestType::MPD:
      adaptiveTree_ = new adaptive::DASHTree(kodiProps);
      break;
    case PROPERTIES::ManifestType::ISM:
      adaptiveTree_ = new adaptive::SmoothTree(kodiProps);
      break;
    case PROPERTIES::ManifestType::HLS:
      adaptiveTree_ = new adaptive::HLSTree(kodiProps, new AESDecrypter(kodiProps.m_licenseKey));
      break;
    default:
      LOG::LogF(LOGFATAL, "Manifest type not handled");
      return;
  };

  representationChooser_ = new DefaultRepresentationChooser();
  representationChooser_->assured_buffer_duration_ =
      kodi::addon::GetSettingInt("ASSUREDBUFFERDURATION");
  representationChooser_->max_buffer_duration_ = kodi::addon::GetSettingInt("MAXBUFFERDURATION");
  adaptiveTree_->representation_chooser_ = representationChooser_;

  std::string fn(profile_path_ + "bandwidth.bin");
  FILE* f = fopen(fn.c_str(), "rb");
  if (f)
  {
    double val;
    size_t sz(fread(&val, sizeof(double), 1, f));
    if (sz)
    {
      representationChooser_->bandwidth_ = static_cast<uint32_t>(val * 8);
      representationChooser_->set_download_speed(val);
    }
    fclose(f);
  }
  else
    representationChooser_->bandwidth_ = 4000000;
  LOG::Log(LOGDEBUG, "Initial bandwidth: %u ", representationChooser_->bandwidth_);

  representationChooser_->max_resolution_ = kodi::addon::GetSettingInt("MAXRESOLUTION");
  LOG::Log(LOGDEBUG, "MAXRESOLUTION selected: %d ",
            representationChooser_->max_resolution_);

  representationChooser_->max_secure_resolution_ =
      kodi::addon::GetSettingInt("MAXRESOLUTIONSECURE");
  LOG::Log(LOGDEBUG, "MAXRESOLUTIONSECURE selected: %d ",
            representationChooser_->max_secure_resolution_);

  manual_streams_ = kodi::addon::GetSettingInt("STREAMSELECTION");
  LOG::Log(LOGDEBUG, "STREAMSELECTION selected: %d ", manual_streams_);

  allow_no_secure_decoder_ = kodi::addon::GetSettingBoolean("NOSECUREDECODER");
  LOG::Log(LOGDEBUG, "FORCENONSECUREDECODER selected: %d ", allow_no_secure_decoder_);

  int buf = kodi::addon::GetSettingInt("MEDIATYPE");
  switch (buf)
  {
    case 1:
      media_type_mask_ = static_cast<uint8_t>(1U) << adaptive::AdaptiveTree::AUDIO;
      break;
    case 2:
      media_type_mask_ = static_cast<uint8_t>(1U) << adaptive::AdaptiveTree::VIDEO;
      break;
    case 3:
      media_type_mask_ = (static_cast<uint8_t>(1U) << adaptive::AdaptiveTree::VIDEO) |
                         (static_cast<uint8_t>(1U) << adaptive::AdaptiveTree::SUBTITLE);
      break;
    default:
      media_type_mask_ = static_cast<uint8_t>(~0);
  }

  buf = kodi::addon::GetSettingInt("MINBANDWIDTH");
  representationChooser_->min_bandwidth_ = buf;
  buf = kodi::addon::GetSettingInt("MAXBANDWIDTH");
  representationChooser_->max_bandwidth_ = buf;

  representationChooser_->ignore_display_ = kodi::addon::GetSettingBoolean("IGNOREDISPLAY");
  representationChooser_->hdcp_override_ = kodi::addon::GetSettingBoolean("HDCPOVERRIDE");
  representationChooser_->ignore_window_change_ =
      kodi::addon::GetSettingBoolean("IGNOREWINDOWCHANGE");

  if (!kodiProps.m_serverCertificate.empty())
  {
    std::string decCert{ BASE64::Decode(kodiProps.m_serverCertificate) };
    server_certificate_.SetData(reinterpret_cast<const AP4_Byte*>(decCert.data()), decCert.size());
  }
}

Session::~Session()
{
  LOG::Log(LOGDEBUG, "Session::~Session()");
  m_streams.clear();

  DisposeDecrypter();

  std::string fn(profile_path_ + "bandwidth.bin");
  FILE* f = fopen(fn.c_str(), "wb");
  if (f)
  {
    double val(representationChooser_->get_average_download_speed());
    fwrite((const char*)&val, sizeof(double), 1, f);
    fclose(f);
  }
  delete adaptiveTree_;
  adaptiveTree_ = nullptr;

  delete representationChooser_;
  representationChooser_ = nullptr;
}

void Session::GetSupportedDecrypterURN(std::string& key_system)
{
  typedef SSD::SSD_DECRYPTER* (*CreateDecryptorInstanceFunc)(SSD::SSD_HOST * host,
                                                             uint32_t version);

  std::string specialpath = kodi::addon::GetSettingString("DECRYPTERPATH");
  if (specialpath.empty())
  {
    LOG::Log(LOGDEBUG, "DECRYPTERPATH not specified in settings.xml");
    return;
  }
  kodihost->SetLibraryPath(kodi::vfs::TranslateSpecialProtocol(specialpath).c_str());

  std::vector<std::string> searchPaths(2);
  searchPaths[0] =
      kodi::vfs::TranslateSpecialProtocol("special://xbmcbinaddons/inputstream.adaptive/");
  searchPaths[1] = kodi::addon::GetAddonInfo("path");

  std::vector<kodi::vfs::CDirEntry> items;

  for (std::vector<std::string>::const_iterator path(searchPaths.begin());
       !decrypter_ && path != searchPaths.end(); ++path)
  {
    LOG::Log(LOGDEBUG, "Searching for decrypters in: %s", path->c_str());

    if (!kodi::vfs::GetDirectory(*path, "", items))
      continue;

    for (unsigned int i(0); i < items.size(); ++i)
    {
      if (strncmp(items[i].Label().c_str(), "ssd_", 4) &&
          strncmp(items[i].Label().c_str(), "libssd_", 7))
        continue;

      bool success = false;
      decrypterModule_ = new kodi::tools::CDllHelper;
      if (decrypterModule_->LoadDll(items[i].Path()))
        {
        CreateDecryptorInstanceFunc startup;
        if (decrypterModule_->RegisterSymbol(startup, "CreateDecryptorInstance"))
          {
            SSD::SSD_DECRYPTER* decrypter = startup(kodihost, SSD::SSD_HOST::version);
            const char* suppUrn(0);

            if (decrypter &&
                (suppUrn = decrypter->SelectKeySytem(m_kodiProps.m_licenseType.c_str())))
            {
              LOG::Log(LOGDEBUG, "Found decrypter: %s", items[i].Path().c_str());
              success = true;
              decrypter_ = decrypter;
              key_system = suppUrn;
              break;
            }
          }
      }
      else
      {
        LOG::Log(LOGDEBUG, "%s", dlerror());
      }
      if (!success)
      {
        delete decrypterModule_;
        decrypterModule_ = 0;
      }
    }
  }
}

void Session::DisposeSampleDecrypter()
{
  if (decrypter_)
  {
    for (std::vector<CDMSESSION>::iterator b(cdm_sessions_.begin()), e(cdm_sessions_.end()); b != e;
         ++b)
    {
      b->cdm_session_str_ = nullptr;
      if (!b->shared_single_sample_decryptor_)
      {
        decrypter_->DestroySingleSampleDecrypter(b->single_sample_decryptor_);
        b->single_sample_decryptor_ = nullptr;
      }
      else
      {
        b->single_sample_decryptor_ = nullptr;
        b->shared_single_sample_decryptor_ = false;
      }
    }
  }
}

void Session::DisposeDecrypter()
{
  if (!decrypterModule_)
    return;

  DisposeSampleDecrypter();

  typedef void (*DeleteDecryptorInstanceFunc)(SSD::SSD_DECRYPTER*);
  DeleteDecryptorInstanceFunc disposefn;
  if (decrypterModule_->RegisterSymbol(disposefn, "DeleteDecryptorInstance"))
    disposefn(decrypter_);

  delete decrypterModule_;
  decrypterModule_ = 0;
  decrypter_ = 0;
}

/*----------------------------------------------------------------------
|   initialize
+---------------------------------------------------------------------*/

bool Session::Initialize(const std::uint8_t config, uint32_t max_user_bandwidth)
{
  if (!adaptiveTree_)
    return false;

  representationChooser_->SetMaxUserBandwidth(max_user_bandwidth);

  // Get URN's wich are supported by this addon
  if (!m_kodiProps.m_licenseType.empty())
  {
    GetSupportedDecrypterURN(adaptiveTree_->supportedKeySystem_);
    LOG::Log(LOGDEBUG, "Supported URN: %s", adaptiveTree_->supportedKeySystem_.c_str());
  }

  // Preinitialize the DRM, if pre-initialisation data are provided
  std::map<std::string, std::string> additionalHeaders = std::map<std::string, std::string>();
  bool isSessionOpened{false};

  if (!m_kodiProps.m_drmPreInitData.empty())
  {
    std::string challengeB64;
    std::string sessionId;
    // Pre-initialize the DRM allow to generate the challenge and session ID data
    // used to make licensed manifest requests (via proxy callback)
    if (PreInitializeDRM(challengeB64, sessionId, isSessionOpened))
    {
      additionalHeaders["challengeB64"] = STRING::URLEncode(challengeB64);
      additionalHeaders["sessionId"] = sessionId;
    }
    else
      return false;
  }

  // Open manifest file with location redirect support  bool mpdSuccess;
  std::string manifestUrl =
      adaptiveTree_->location_.empty() ? manifestURL_ : adaptiveTree_->location_;
  if (!adaptiveTree_->open(manifestUrl, m_kodiProps.m_manifestUpdateParam, additionalHeaders) ||
      adaptiveTree_->empty())
  {
    LOG::Log(LOGERROR, "Could not open / parse manifest (%s)", manifestUrl.c_str());
    return false;
  }
  LOG::Log(LOGINFO,
            "Successfully parsed manifest file. #Periods: %ld, #Streams in first period: %ld, Type: "
            "%s, Download speed: %0.4f Bytes/s",
            adaptiveTree_->periods_.size(), adaptiveTree_->current_period_->adaptationSets_.size(),
            adaptiveTree_->has_timeshift_buffer_ ? "live" : "VOD",
            representationChooser_->download_speed_);

  drmConfig_ = config;

  // Always need at least 16s delay from live
  if (adaptiveTree_->live_delay_ < 16)
    adaptiveTree_->live_delay_ = 16;

  return InitializePeriod(isSessionOpened);
}

bool Session::PreInitializeDRM(std::string& challengeB64,
                               std::string& sessionId,
                               bool& isSessionOpened)
{
  std::string psshData;
  std::string kidData;
  // Parse the PSSH/KID data
  std::string::size_type posSplitter(m_kodiProps.m_drmPreInitData.find("|"));
  if (posSplitter != std::string::npos)
  {
    psshData = m_kodiProps.m_drmPreInitData.substr(0, posSplitter);
    kidData = m_kodiProps.m_drmPreInitData.substr(posSplitter + 1);
  }

  if (psshData.empty() || kidData.empty())
  {
    LOG::LogF(LOGERROR, "Invalid DRM pre-init data, must be as: {PSSH as base64}|{KID as base64}");
    return false;
  }

  cdm_sessions_.resize(2);
  memset(&cdm_sessions_.front(), 0, sizeof(CDMSESSION));
  // Try to initialize an SingleSampleDecryptor
  LOG::LogF(LOGDEBUG, "Entering encryption section");

  if (m_kodiProps.m_licenseKey.empty())
  {
    LOG::LogF(LOGERROR, "Invalid license_key");
    return false;
  }

  if (!decrypter_)
  {
    LOG::LogF(LOGERROR, "No decrypter found for encrypted stream");
    return false;
  }

  if (!decrypter_->HasCdmSession())
  {
    if (!decrypter_->OpenDRMSystem(m_kodiProps.m_licenseKey.c_str(), server_certificate_,
                                   drmConfig_))
    {
      LOG::LogF(LOGERROR, "OpenDRMSystem failed");
      return false;
    }
  }

  AP4_DataBuffer init_data;
  init_data.SetBufferSize(1024);
  const char* optionalKeyParameter(nullptr);

  // Set the provided PSSH
  std::string decPssh{BASE64::Decode(psshData)};
  init_data.SetData(reinterpret_cast<const AP4_Byte*>(decPssh.data()), decPssh.size());

  // Decode the provided KID
  std::string decKid{BASE64::Decode(kidData)};

  CDMSESSION& session(cdm_sessions_[1]);

  std::string hexKid{StringUtils::ToHexadecimal(decKid)};
  LOG::LogF(LOGDEBUG, "Initializing session with KID: %s", hexKid.c_str());

  if (decrypter_ && init_data.GetDataSize() >= 4 &&
      (session.single_sample_decryptor_ = decrypter_->CreateSingleSampleDecrypter(
           init_data, optionalKeyParameter, decKid, true)) != 0)
  {
    session.cdm_session_str_ = session.single_sample_decryptor_->GetSessionId();
    sessionId = session.cdm_session_str_;
    challengeB64 = decrypter_->GetChallengeB64Data(session.single_sample_decryptor_);
  }
  else
  {
    LOG::LogF(LOGERROR, "Initialize failed (SingleSampleDecrypter)");
    session.single_sample_decryptor_ = nullptr;
    return false;
  }
#if defined(ANDROID)
  // On android is not possible add the default KID key
  // then we cannot re-use same session
  DisposeSampleDecrypter();
#else
  isSessionOpened = true;
#endif
  return true;
}

bool Session::InitializeDRM(bool addDefaultKID /* = false */)
{
  bool secure_video_session = false;
  cdm_sessions_.resize(adaptiveTree_->current_period_->psshSets_.size());
  memset(&cdm_sessions_.front(), 0, sizeof(CDMSESSION));

  representationChooser_->decrypter_caps_.resize(cdm_sessions_.size());
  for (const Session::CDMSESSION& cdmsession : cdm_sessions_)
    representationChooser_->decrypter_caps_.push_back(cdmsession.decrypter_caps_);

  // Try to initialize an SingleSampleDecryptor
  if (adaptiveTree_->current_period_->encryptionState_)
  {
    std::string licenseKey{m_kodiProps.m_licenseKey};

    if (licenseKey.empty())
      licenseKey = adaptiveTree_->license_url_;

    LOG::Log(LOGDEBUG, "Entering encryption section");

    if (licenseKey.empty())
    {
      LOG::Log(LOGERROR, "Invalid license_key");
      return false;
    }

    if (!decrypter_)
    {
      LOG::Log(LOGERROR, "No decrypter found for encrypted stream");
      return false;
    }

    if (!decrypter_->HasCdmSession())
    {
      if (!decrypter_->OpenDRMSystem(licenseKey.c_str(), server_certificate_, drmConfig_))
      {
        LOG::Log(LOGERROR, "OpenDRMSystem failed");
        return false;
      }
    }
    std::string strkey(adaptiveTree_->supportedKeySystem_.substr(9));
    size_t pos;
    while ((pos = strkey.find('-')) != std::string::npos)
      strkey.erase(pos, 1);
    if (strkey.size() != 32)
    {
      LOG::Log(LOGERROR, "Key system mismatch (%s)!",
                adaptiveTree_->supportedKeySystem_.c_str());
      return false;
    }

    unsigned char key_system[16];
    AP4_ParseHex(strkey.c_str(), key_system, 16);
    uint32_t currentSessionTypes = 0;

    for (size_t ses(1); ses < cdm_sessions_.size(); ++ses)
    {
      AP4_DataBuffer init_data;
      const char* optionalKeyParameter(nullptr);
      adaptive::AdaptiveTree::Period::PSSH sessionPsshset =
        adaptiveTree_->current_period_->psshSets_[ses];
      uint32_t sessionType = 0;

      if (sessionPsshset.media_ > 0)
      {
        sessionType = sessionPsshset.media_;
      }
      else
      {
        switch (sessionPsshset.adaptation_set_->type_)
        {
          case adaptive::AdaptiveTree::VIDEO:
            sessionType = adaptive::AdaptiveTree::Period::PSSH::MEDIA_VIDEO;
            break;
          case adaptive::AdaptiveTree::AUDIO:
            sessionType = adaptive::AdaptiveTree::Period::PSSH::MEDIA_AUDIO;
            break;
          default:
            break;
        }
      }

      if (sessionPsshset.pssh_ == "FILE")
      {
        LOG::Log(LOGDEBUG, "Searching PSSH data in FILE");

        if (m_kodiProps.m_licenseData.empty())
        {
          Session::STREAM stream(*adaptiveTree_, sessionPsshset.adaptation_set_, media_headers_,
                                 representationChooser_, m_kodiProps.m_playTimeshiftBuffer, 0, false);

          stream.enabled = true;
          stream.m_kodiAdStream.start_stream();
          stream.SetAdByteStream(std::make_unique<AdaptiveByteStream>(&stream.m_kodiAdStream));
          stream.SetStreamFile(std::make_unique<AP4_File>(*stream.GetAdByteStream(),
                                                          AP4_DefaultAtomFactory::Instance_, true));
          AP4_Movie* movie = stream.GetStreamFile()->GetMovie();
          if (movie == NULL)
          {
            LOG::Log(LOGERROR, "No MOOV in stream!");
            stream.disable();
            return false;
          }
          AP4_Array<AP4_PsshAtom>& pssh = movie->GetPsshAtoms();

          for (unsigned int i = 0; !init_data.GetDataSize() && i < pssh.ItemCount(); i++)
          {
            if (memcmp(pssh[i].GetSystemId(), key_system, 16) == 0)
            {
              init_data.AppendData(pssh[i].GetData().GetData(), pssh[i].GetData().GetDataSize());
              if (sessionPsshset.defaultKID_.empty())
              {
                if (pssh[i].GetKid(0))
                  sessionPsshset.defaultKID_ =
                      std::string((const char*)pssh[i].GetKid(0), 16);
                else if (AP4_Track* track = movie->GetTrack(TIDC[stream.m_kodiAdStream.get_type()]))
                {
                  AP4_ProtectedSampleDescription* m_protectedDesc =
                      static_cast<AP4_ProtectedSampleDescription*>(track->GetSampleDescription(0));
                  AP4_ContainerAtom* schi;
                  if (m_protectedDesc->GetSchemeInfo() &&
                      (schi = m_protectedDesc->GetSchemeInfo()->GetSchiAtom()))
                  {
                    AP4_TencAtom* tenc(
                        AP4_DYNAMIC_CAST(AP4_TencAtom, schi->GetChild(AP4_ATOM_TYPE_TENC, 0)));
                    if (tenc)
                    {
                      sessionPsshset.defaultKID_ =
                          std::string(reinterpret_cast<const char*>(tenc->GetDefaultKid()), 16);
                    }
                    else
                    {
                      AP4_PiffTrackEncryptionAtom* piff(
                          AP4_DYNAMIC_CAST(AP4_PiffTrackEncryptionAtom,
                                           schi->GetChild(AP4_UUID_PIFF_TRACK_ENCRYPTION_ATOM, 0)));
                      if (piff)
                      {
                        sessionPsshset.defaultKID_ =
                            std::string(reinterpret_cast<const char*>(piff->GetDefaultKid()), 16);
                      }
                    }
                  }
                }
              }
            }
          }

          if (!init_data.GetDataSize())
          {
            LOG::Log(LOGERROR,
                      "Could not extract license from video stream (PSSH not found)");
            stream.disable();
            return false;
          }
          stream.disable();
        }
        else if (!sessionPsshset.defaultKID_.empty())
        {
          init_data.SetData(reinterpret_cast<AP4_Byte*>(sessionPsshset.defaultKID_.data()), 16);

          std::string decLicenseData{BASE64::Decode(m_kodiProps.m_licenseData)};
          uint8_t* decLicDataUInt = reinterpret_cast<uint8_t*>(decLicenseData.data());

          uint8_t* uuid(reinterpret_cast<uint8_t*>(strstr(decLicenseData.data(), "{KID}")));
          if (uuid)
          {
            memmove(uuid + 11, uuid, m_kodiProps.m_licenseData.size() - (uuid - decLicDataUInt));
            memcpy(uuid, init_data.GetData(), init_data.GetDataSize());
            init_data.SetData(decLicDataUInt, m_kodiProps.m_licenseData.size() + 11);
          }
          else
            init_data.SetData(decLicDataUInt, m_kodiProps.m_licenseData.size());
        }
        else
          return false;
      }
      else
      {
        if (m_kodiProps.m_manifestType == PROPERTIES::ManifestType::ISM)
        {
          if (m_kodiProps.m_licenseType == "com.widevine.alpha")
          {
            std::string licenseData{m_kodiProps.m_licenseData};
            if (licenseData.empty())
              licenseData = "e0tJRH0="; // {KID}
            std::vector<uint8_t> init_data_v;
            CreateISMlicense(sessionPsshset.defaultKID_,
                             licenseData, init_data_v);
            init_data.SetData(init_data_v.data(), init_data_v.size());
          }
          else
          {
            init_data.SetData(reinterpret_cast<const uint8_t*>(
                                  sessionPsshset.pssh_.data()),
                              sessionPsshset.pssh_.size());
            optionalKeyParameter =
                m_kodiProps.m_licenseData.empty() ? nullptr : m_kodiProps.m_licenseData.c_str();
          }
        }
        else
        {
          std::string decPssh{BASE64::Decode(sessionPsshset.pssh_)};
          init_data.SetBufferSize(1024);
          init_data.SetData(reinterpret_cast<const AP4_Byte*>(decPssh.data()), decPssh.size());
        }
      }

      CDMSESSION& session(cdm_sessions_[ses]);
      std::string defaultKid{sessionPsshset.defaultKID_};

      const char* defkid = sessionPsshset.defaultKID_.empty()
                               ? nullptr
                               : sessionPsshset.defaultKID_.data();

      if (addDefaultKID && ses == 1 && session.single_sample_decryptor_)
      {
        // If the CDM has been pre-initialized, on non-android systems
        // we use the same session opened then we have to add the current KID
        // because the session has been opened with a different PSSH/KID
        session.single_sample_decryptor_->AddKeyId(defaultKid);
        session.single_sample_decryptor_->SetDefaultKeyId(defaultKid);
      }

      if (decrypter_ && !defaultKid.empty())
      {
        std::string hexKid{StringUtils::ToHexadecimal(defaultKid)};
        LOG::Log(LOGDEBUG, "Initializing stream with KID: %s", hexKid.c_str());

        // use shared ssd session if we already have 1 of the same stream type
        if (currentSessionTypes & sessionType)
        {
          for (unsigned int i(1); i < ses; ++i)
          {
            if (decrypter_->HasLicenseKey(cdm_sessions_[i].single_sample_decryptor_,
                                          reinterpret_cast<const uint8_t*>(defkid)))
            {
              session.single_sample_decryptor_ = cdm_sessions_[i].single_sample_decryptor_;
              session.shared_single_sample_decryptor_ = true;
              break;
            }
          }
        }
      }
      else if (!defkid && !session.single_sample_decryptor_)
      {
          LOG::Log(LOGWARNING, "Initializing stream with unknown KID!");
      }

      currentSessionTypes |= sessionType;

      if (decrypter_ && init_data.GetDataSize() >= 4 &&
          (session.single_sample_decryptor_ ||
           (session.single_sample_decryptor_ = decrypter_->CreateSingleSampleDecrypter(
                init_data, optionalKeyParameter, defaultKid, false)) != 0))
      {
        decrypter_->GetCapabilities(
            session.single_sample_decryptor_, reinterpret_cast<const uint8_t*>(defaultKid.c_str()),
            sessionPsshset.media_, session.decrypter_caps_);

        if (session.decrypter_caps_.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_INVALID)
          adaptiveTree_->current_period_->RemovePSSHSet(static_cast<std::uint16_t>(ses));
        else if (session.decrypter_caps_.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH)
        {
          session.cdm_session_str_ = session.single_sample_decryptor_->GetSessionId();
          secure_video_session = true;

          if (allow_no_secure_decoder_ && !m_kodiProps.m_isLicenseForceSecureDecoder &&
              !adaptiveTree_->current_period_->need_secure_decoder_)
            session.decrypter_caps_.flags &= ~SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_DECODER;
        }
      }
      else
      {
        LOG::Log(LOGERROR, "Initialize failed (SingleSampleDecrypter)");
        for (unsigned int i(ses); i < cdm_sessions_.size(); ++i)
          cdm_sessions_[i].single_sample_decryptor_ = nullptr;
        return false;
      }
    }
  }
  representationChooser_->Prepare(secure_video_session);

  return true;
}

bool Session::InitializePeriod(bool isSessionOpened /* = false */)
{
  bool psshChanged = true;
  if (adaptiveTree_->next_period_)
  {
    psshChanged =
        !(adaptiveTree_->current_period_->psshSets_ == adaptiveTree_->next_period_->psshSets_);
    adaptiveTree_->current_period_ = adaptiveTree_->next_period_;
    adaptiveTree_->next_period_ = nullptr;
  }

  chapter_start_time_ = GetChapterStartTime();

  if (adaptiveTree_->current_period_->encryptionState_ ==
      adaptive::AdaptiveTree::ENCRYTIONSTATE_ENCRYPTED)
  {
    LOG::Log(LOGERROR, "Unable to handle decryption. Unsupported!");
    return false;
  }

  // create SESSION::STREAM objects. One for each AdaptationSet
  unsigned int i(0);
  adaptive::AdaptiveTree::AdaptationSet* adp;

  m_streams.clear();

  if (!psshChanged)
    LOG::Log(LOGDEBUG, "Reusing DRM psshSets for new period!");
  else
  {
    if (isSessionOpened)
    {
      LOG::Log(LOGDEBUG, "New period, reinitialize by using same session");
    }
    else
    {
      LOG::Log(LOGDEBUG, "New period, dispose sample decrypter and reinitialize");
      DisposeSampleDecrypter();
    }
    if (!InitializeDRM(isSessionOpened))
      return false;
  }

  while ((adp = adaptiveTree_->GetAdaptationSet(i++)))
  {
    if (adp->representations_.empty())
      continue;

    bool manual_streams = adp->type_ == adaptive::AdaptiveTree::StreamType::VIDEO
                              ? manual_streams_ != 0
                              : manual_streams_ == 1;

    // Select good video stream
    adaptive::AdaptiveTree::Representation* defaultRepresentation =
        adaptiveTree_->ChooseRepresentation(adp);
    size_t repId = manual_streams ? adp->representations_.size() : 0;

    do
    {
      m_streams.push_back(std::make_unique<STREAM>(
          *adaptiveTree_, adp, media_headers_, representationChooser_,
          m_kodiProps.m_playTimeshiftBuffer, repId, first_period_initialized_));
      STREAM& stream(*m_streams.back());

      uint32_t flags = INPUTSTREAM_FLAG_NONE;
      size_t copySize = adp->name_.size() > 255 ? 255 : adp->name_.size();
      stream.info_.SetName(adp->name_);

      switch (adp->type_)
      {
        case adaptive::AdaptiveTree::VIDEO:
          stream.info_.SetStreamType(INPUTSTREAM_TYPE_VIDEO);
          if (manual_streams && stream.m_kodiAdStream.getRepresentation() == defaultRepresentation)
            flags |= INPUTSTREAM_FLAG_DEFAULT;
          break;
        case adaptive::AdaptiveTree::AUDIO:
          stream.info_.SetStreamType(INPUTSTREAM_TYPE_AUDIO);
          if (adp->impaired_)
            flags |= INPUTSTREAM_FLAG_VISUAL_IMPAIRED;
          if (adp->default_)
            flags |= INPUTSTREAM_FLAG_DEFAULT;
          if (adp->original_ || (!m_kodiProps.m_audioLanguageOrig.empty() &&
                                 adp->language_ == m_kodiProps.m_audioLanguageOrig))
            flags |= INPUTSTREAM_FLAG_ORIGINAL;
          break;
        case adaptive::AdaptiveTree::SUBTITLE:
          stream.info_.SetStreamType(INPUTSTREAM_TYPE_SUBTITLE);
          if (adp->impaired_)
            flags |= INPUTSTREAM_FLAG_HEARING_IMPAIRED;
          if (adp->forced_)
            flags |= INPUTSTREAM_FLAG_FORCED;
          if (adp->default_)
            flags |= INPUTSTREAM_FLAG_DEFAULT;
          break;
        default:
          break;
      }
      stream.info_.SetFlags(flags);
      stream.info_.SetPhysicalIndex(i | (repId << 16));
      stream.info_.SetLanguage(adp->language_);
      stream.info_.ClearExtraData();
      stream.info_.SetFeatures(0);
      stream.m_kodiAdStream.set_observer(dynamic_cast<adaptive::AdaptiveStreamObserver*>(this));

      UpdateStream(stream);

    } while (repId-- != (manual_streams ? 1 : 0));
  }
  first_period_initialized_ = true;
  return true;
}

void Session::UpdateStream(STREAM& stream)
{
  const adaptive::AdaptiveTree::Representation* rep(stream.m_kodiAdStream.getRepresentation());
  const SSD::SSD_DECRYPTER::SSD_CAPS& caps = GetDecrypterCaps(rep->pssh_set_);

  stream.info_.SetWidth(rep->width_);
  stream.info_.SetHeight(rep->height_);
  stream.info_.SetAspect(rep->aspect_);

  if (stream.info_.GetAspect() == 0.0f && stream.info_.GetHeight())
    stream.info_.SetAspect((float)stream.info_.GetWidth() / stream.info_.GetHeight());
  stream.encrypted = rep->get_psshset() > 0;

  stream.info_.SetExtraData(nullptr, 0);
  if (rep->codec_private_data_.size())
  {
    std::string annexb;
    const std::string* res(&annexb);

    if ((caps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_ANNEXB_REQUIRED) &&
        stream.info_.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
    {
      LOG::Log(LOGDEBUG, "UpdateStream: Convert avc -> annexb");
      annexb = AvcToAnnexb(rep->codec_private_data_);
    }
    else
      res = &rep->codec_private_data_;

    stream.info_.SetExtraData(reinterpret_cast<const uint8_t*>(res->data()), res->size());
  }

  // we currently use only the first track!
  std::string::size_type pos = rep->codecs_.find(",");
  if (pos == std::string::npos)
    pos = rep->codecs_.size();

  stream.info_.SetCodecInternalName(rep->codecs_);
  stream.info_.SetCodecFourCC(0);

#if INPUTSTREAM_VERSION_LEVEL > 0
  stream.info_.SetColorSpace(INPUTSTREAM_COLORSPACE_UNSPECIFIED);
  stream.info_.SetColorRange(INPUTSTREAM_COLORRANGE_UNKNOWN);
  stream.info_.SetColorPrimaries(INPUTSTREAM_COLORPRIMARY_UNSPECIFIED);
  stream.info_.SetColorTransferCharacteristic(INPUTSTREAM_COLORTRC_UNSPECIFIED);
#else
  stream.info_.SetColorSpace(INPUTSTREAM_COLORSPACE_UNKNOWN);
  stream.info_.SetColorRange(INPUTSTREAM_COLORRANGE_UNKNOWN);
#endif
  if (rep->codecs_.find("mp4a") == 0 || rep->codecs_.find("aac") == 0)
    stream.info_.SetCodecName("aac");
  else if (rep->codecs_.find("dts") == 0)
    stream.info_.SetCodecName("dca");
  else if (rep->codecs_.find("ac-3") == 0)
    stream.info_.SetCodecName("ac3");
  else if (rep->codecs_.find("ec-3") == 0)
    stream.info_.SetCodecName("eac3");
  else if (rep->codecs_.find("avc") == 0 || rep->codecs_.find("h264") == 0)
    stream.info_.SetCodecName("h264");
  else if (rep->codecs_.find("hev") == 0)
    stream.info_.SetCodecName("hevc");
  else if (rep->codecs_.find("hvc") == 0 || rep->codecs_.find("dvh") == 0)
  {
    stream.info_.SetCodecFourCC(
        MakeFourCC(rep->codecs_[0], rep->codecs_[1], rep->codecs_[2], rep->codecs_[3]));
    stream.info_.SetCodecName("hevc");
  }
  else if (rep->codecs_.find("vp9") == 0 || rep->codecs_.find("vp09") == 0)
  {
    stream.info_.SetCodecName("vp9");
#if INPUTSTREAM_VERSION_LEVEL > 0
    if ((pos = rep->codecs_.find(".")) != std::string::npos)
      stream.info_.SetCodecProfile(static_cast<STREAMCODEC_PROFILE>(
          VP9CodecProfile0 + atoi(rep->codecs_.c_str() + (pos + 1))));
#endif
  }
  else if (rep->codecs_.find("opus") == 0)
    stream.info_.SetCodecName("opus");
  else if (rep->codecs_.find("vorbis") == 0)
    stream.info_.SetCodecName("vorbis");
  else if (rep->codecs_.find("stpp") == 0 || rep->codecs_.find("ttml") == 0)
    stream.info_.SetCodecName("srt");
  else if (rep->codecs_.find("wvtt") == 0)
    stream.info_.SetCodecName("webvtt");
  else
    stream.valid = false;

  // We support currently only mp4 / ts / adts
  if (rep->containerType_ != adaptive::AdaptiveTree::CONTAINERTYPE_NOTYPE &&
      rep->containerType_ != adaptive::AdaptiveTree::CONTAINERTYPE_MP4 &&
      rep->containerType_ != adaptive::AdaptiveTree::CONTAINERTYPE_TS &&
      rep->containerType_ != adaptive::AdaptiveTree::CONTAINERTYPE_ADTS &&
      rep->containerType_ != adaptive::AdaptiveTree::CONTAINERTYPE_WEBM &&
      rep->containerType_ != adaptive::AdaptiveTree::CONTAINERTYPE_TEXT)
    stream.valid = false;

  stream.info_.SetFpsRate(rep->fpsRate_);
  stream.info_.SetFpsScale(rep->fpsScale_);
  stream.info_.SetSampleRate(rep->samplingRate_);
  stream.info_.SetChannels(rep->channelCount_);
  stream.info_.SetBitRate(rep->bandwidth_);
}

AP4_Movie* Session::PrepareStream(STREAM* stream, bool& needRefetch)
{
  needRefetch = false;
  switch (adaptiveTree_->prepareRepresentation(stream->m_kodiAdStream.getPeriod(),
                                               stream->m_kodiAdStream.getAdaptationSet(),
                                               stream->m_kodiAdStream.getRepresentation()))
  {
    case adaptive::AdaptiveTree::PREPARE_RESULT_FAILURE:
      return nullptr;
    case adaptive::AdaptiveTree::PREPARE_RESULT_DRMCHANGED:
      if (!InitializeDRM())
        return nullptr;
    case adaptive::AdaptiveTree::PREPARE_RESULT_DRMUNCHANGED:
      stream->encrypted = stream->m_kodiAdStream.getRepresentation()->pssh_set_ > 0;
      needRefetch = true;
      break;
    default:;
  }

  if (stream->m_kodiAdStream.getRepresentation()->containerType_ ==
          adaptive::AdaptiveTree::CONTAINERTYPE_MP4 &&
      (stream->m_kodiAdStream.getRepresentation()->flags_ &
       adaptive::AdaptiveTree::Representation::INITIALIZATION_PREFIXED) == 0 &&
      stream->m_kodiAdStream.getRepresentation()->get_initialization() == nullptr)
  {
    //We'll create a Movie out of the things we got from manifest file
    //note: movie will be deleted in destructor of stream->input_file_
    AP4_Movie* movie = new AP4_Movie();

    AP4_SyntheticSampleTable* sample_table = new AP4_SyntheticSampleTable();

    AP4_SampleDescription* sample_descryption;
    if (stream->info_.GetCodecName() == "h264")
    {
      const std::string& extradata(stream->m_kodiAdStream.getRepresentation()->codec_private_data_);
      AP4_MemoryByteStream ms((const uint8_t*)extradata.data(), extradata.size());
      AP4_AvccAtom* atom = AP4_AvccAtom::Create(AP4_ATOM_HEADER_SIZE + extradata.size(), ms);
      sample_descryption =
          new AP4_AvcSampleDescription(AP4_SAMPLE_FORMAT_AVC1, stream->info_.GetWidth(),
                                       stream->info_.GetHeight(), 0, nullptr, atom);
    }
    else if (stream->info_.GetCodecName() == "hevc")
    {
      const std::string& extradata(stream->m_kodiAdStream.getRepresentation()->codec_private_data_);
      AP4_MemoryByteStream ms((const uint8_t*)extradata.data(), extradata.size());
      AP4_HvccAtom* atom = AP4_HvccAtom::Create(AP4_ATOM_HEADER_SIZE + extradata.size(), ms);
      sample_descryption =
          new AP4_HevcSampleDescription(AP4_SAMPLE_FORMAT_HEV1, stream->info_.GetWidth(),
                                        stream->info_.GetHeight(), 0, nullptr, atom);
    }
    else if (stream->info_.GetCodecName() == "srt")
      sample_descryption = new AP4_SampleDescription(AP4_SampleDescription::TYPE_SUBTITLES,
                                                     AP4_SAMPLE_FORMAT_STPP, 0);
    else
      sample_descryption = new AP4_SampleDescription(AP4_SampleDescription::TYPE_UNKNOWN, 0, 0);

    if (stream->m_kodiAdStream.getRepresentation()->get_psshset() > 0)
    {
      AP4_ContainerAtom schi(AP4_ATOM_TYPE_SCHI);
      schi.AddChild(
          new AP4_TencAtom(AP4_CENC_CIPHER_AES_128_CTR, 8,
                           GetDefaultKeyId(stream->m_kodiAdStream.getRepresentation()->get_psshset())));
      sample_descryption = new AP4_ProtectedSampleDescription(
          0, sample_descryption, 0, AP4_PROTECTION_SCHEME_TYPE_PIFF, 0, "", &schi);
    }
    sample_table->AddSampleDescription(sample_descryption);

    movie->AddTrack(new AP4_Track(TIDC[stream->m_kodiAdStream.get_type()], sample_table,
                                  FragmentedSampleReader::TRACKID_UNKNOWN,
                                  stream->m_kodiAdStream.getRepresentation()->timescale_, 0,
                                  stream->m_kodiAdStream.getRepresentation()->timescale_, 0, "", 0, 0));
    //Create a dumy MOOV Atom to tell Bento4 its a fragmented stream
    AP4_MoovAtom* moov = new AP4_MoovAtom();
    moov->AddChild(new AP4_ContainerAtom(AP4_ATOM_TYPE_MVEX));
    movie->SetMoovAtom(moov);
    return movie;
  }
  return nullptr;
}

void Session::EnableStream(STREAM* stream, bool enable)
{
  if (enable)
  {
    if (!timing_stream_)
      timing_stream_ = stream;
    stream->enabled = true;
  }
  else
  {
    if (stream == timing_stream_)
      timing_stream_ = nullptr;
    stream->disable();
  }
}

uint64_t Session::PTSToElapsed(uint64_t pts)
{
  if (timing_stream_)
  {
    SampleReader* timingReader = timing_stream_->GetReader();
    if (!timingReader) {
      LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
      return 0;
    }

    int64_t manifest_time = static_cast<int64_t>(pts) - timingReader->GetPTSDiff();
    if (manifest_time < 0)
      manifest_time = 0;

    if (static_cast<uint64_t>(manifest_time) > timing_stream_->m_kodiAdStream.GetAbsolutePTSOffset())
      return static_cast<uint64_t>(manifest_time) - timing_stream_->m_kodiAdStream.GetAbsolutePTSOffset();

    return 0ULL;
  }
  else
    return pts;
}

uint64_t Session::GetTimeshiftBufferStart()
{
  if (timing_stream_)
  {
    SampleReader* timingReader = timing_stream_->GetReader();
    if (!timingReader)
    {
      LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
      return 0;
    }
    return timing_stream_->m_kodiAdStream.GetAbsolutePTSOffset() + timingReader->GetPTSDiff();
  }
  else
    return 0ULL;
}

void Session::StartReader(
    STREAM* stream, uint64_t seekTimeCorrected, int64_t ptsDiff, bool preceeding, bool timing)
{
  bool bReset = true;
  if (timing)
    seekTimeCorrected += stream->m_kodiAdStream.GetAbsolutePTSOffset();
  else
    seekTimeCorrected -= ptsDiff;
  stream->m_kodiAdStream.seek_time(
      static_cast<double>(seekTimeCorrected / STREAM_TIME_BASE),
      preceeding, bReset);

  SampleReader* streamReader = stream->GetReader();
  if (!streamReader) {
    LOG::LogF(LOGERROR, "Cannot get the stream reader");
    return;
  }

  if (bReset)
    streamReader->Reset(false);

  bool bStarted = false;
  streamReader->Start(bStarted);
  if (bStarted && (streamReader->GetInformation(stream->info_)))
    changed_ = true;
}

void Session::SetVideoResolution(unsigned int w, unsigned int h)
{
  representationChooser_->SetDisplayDimensions(w, h);
};

SampleReader* Session::GetNextSample()
{
  STREAM* res{nullptr};
  STREAM* waiting{nullptr};

  for (auto& stream : m_streams)
  {
    bool isStarted{false};
    SampleReader* streamReader = stream->GetReader();
    if (!streamReader)
      continue;

    if (stream->enabled && !streamReader->EOS() && AP4_SUCCEEDED(streamReader->Start(isStarted)))
    {
      if (!res || streamReader->DTSorPTS() < res->GetReader()->DTSorPTS())
      {
        if (stream->m_kodiAdStream.waitingForSegment(true))
          waiting = stream.get();
        else
          res = stream.get();
      }
    }

    if (isStarted && streamReader->GetInformation(stream->info_))
      changed_ = true;
  }

  if (res)
  {
    CheckFragmentDuration(*res);
    if (res->GetReader()->GetInformation(res->info_))
      changed_ = true;
    if (res->GetReader()->PTS() != STREAM_NOPTS_VALUE)
      elapsed_time_ = PTSToElapsed(res->GetReader()->PTS()) + GetChapterStartTime();
    return res->GetReader();
  }
  else if (waiting)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return &DummyReader;
  }
  return nullptr;
}

bool Session::SeekTime(double seekTime, unsigned int streamId, bool preceeding)
{
  bool ret(false);

  //we don't have pts < 0 here and work internally with uint64
  if (seekTime < 0)
    seekTime = 0;

  // Check if we leave our current period
  double chapterTime(0);
  std::vector<adaptive::AdaptiveTree::Period*>::const_iterator pi;
  for (pi = adaptiveTree_->periods_.cbegin(); pi != adaptiveTree_->periods_.cend(); ++pi)
  {
    chapterTime += double((*pi)->duration_) / (*pi)->timescale_;
    if (chapterTime > seekTime)
      break;
  }

  if (pi == adaptiveTree_->periods_.end())
    --pi;
  chapterTime -= double((*pi)->duration_) / (*pi)->timescale_;

  if ((*pi) != adaptiveTree_->current_period_)
  {
    LOG::Log(LOGDEBUG, "SeekTime: seeking into new chapter: %d",
              static_cast<int>((pi - adaptiveTree_->periods_.begin()) + 1));
    SeekChapter((pi - adaptiveTree_->periods_.begin()) + 1);
    chapter_seek_time_ = seekTime;
    return true;
  }

  seekTime -= chapterTime;

  // don't try to seek past the end of the stream, leave a sensible amount so we can buffer properly
  if (adaptiveTree_->has_timeshift_buffer_)
  {
    double maxSeek(0);
    uint64_t curTime, maxTime(0);
    for (auto& stream : m_streams)
    {
      if (stream->enabled && (curTime = stream->m_kodiAdStream.getMaxTimeMs()) && curTime > maxTime)
      {
        maxTime = curTime;
      }
    }

    maxSeek = (static_cast<double>(maxTime) / 1000) - adaptiveTree_->live_delay_;
    if (maxSeek < 0)
      maxSeek = 0;

    if (seekTime > maxSeek)
      seekTime = maxSeek;
  }

  // correct for starting segment pts value of chapter and chapter offset within program
  uint64_t seekTimeCorrected = static_cast<uint64_t>(seekTime * STREAM_TIME_BASE);
  int64_t ptsDiff = 0;
  if (timing_stream_)
  {
    // after seeking across chapters with fmp4 streams the reader will not have started
    // so we start here to ensure that we have the required information to correctly
    // seek with proper stream alignment
    SampleReader* timingReader = timing_stream_->GetReader();
    if (!timingReader)
    {
      LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
      return false;
    }
    if (!timingReader->IsStarted())
      StartReader(timing_stream_, seekTimeCorrected, ptsDiff, preceeding, true);

    seekTimeCorrected += timing_stream_->m_kodiAdStream.GetAbsolutePTSOffset();
    ptsDiff = timingReader->GetPTSDiff();
    if (ptsDiff < 0 && seekTimeCorrected + ptsDiff > seekTimeCorrected)
      seekTimeCorrected = 0;
    else
      seekTimeCorrected += ptsDiff;
  }

  for (auto& stream : m_streams)
  {
    SampleReader* streamReader = stream->GetReader();
    if (!streamReader)
    {
      LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
      continue;
    }
    if (stream->enabled && (streamId == 0 || stream->info_.GetPhysicalIndex() == streamId))
    {
      bool reset{true};
      // all streams must be started before seeking to ensure cross chapter seeks
      // will seek to the correct location/segment
      if (!streamReader->IsStarted())
        StartReader(stream.get(), seekTimeCorrected, ptsDiff, preceeding, false);

      double seekSecs{static_cast<double>(seekTimeCorrected - streamReader->GetPTSDiff()) /
                      STREAM_TIME_BASE};
      if (stream->m_kodiAdStream.seek_time(seekSecs, preceeding, reset))
      {
        if (reset)
          streamReader->Reset(false);
        // advance reader to requested time
        if (!streamReader->TimeSeek(seekTimeCorrected, preceeding))
        {
          streamReader->Reset(true);
        }
        else
        {
          double destTime{static_cast<double>(PTSToElapsed(streamReader->PTS())) /
                          STREAM_TIME_BASE};
          LOG::Log(LOGINFO, "Seek time (%0.1lf) for stream: %d continues at %0.1lf (PTS: %llu)",
                   seekTime, stream->info_.GetPhysicalIndex(), destTime, streamReader->PTS());
          if (stream->info_.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
          {
            seekTime = destTime;
            seekTimeCorrected = streamReader->PTS();
            preceeding = false;
          }
          ret = true;
        }
      }
      else
        streamReader->Reset(true);
    }
  }

  return ret;
}

void Session::OnSegmentChanged(adaptive::AdaptiveStream* adStream)
{
  for (auto& stream : m_streams)
  {
    if (&stream->m_kodiAdStream == adStream)
    {
      SampleReader* streamReader = stream->GetReader();
      if (!streamReader)
        LOG::LogF(LOGWARNING, "Cannot get the stream sample reader");
      else
        streamReader->SetPTSOffset(stream->m_kodiAdStream.GetCurrentPTSOffset());

      stream->segmentChanged = true;
      break;
    }
  }
}

void Session::OnStreamChange(adaptive::AdaptiveStream* adStream)
{
  for (auto& stream : m_streams)
  {
    if (stream->enabled && &stream->m_kodiAdStream == adStream)
    {
      UpdateStream(*stream);
      changed_ = true;
    }
  }
}

void Session::CheckFragmentDuration(STREAM& stream)
{
  uint64_t nextTs;
  uint64_t nextDur;
  SampleReader* streamReader = stream.GetReader();
  if (!streamReader)
  {
    LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
    return;
  }

  if (stream.segmentChanged && streamReader->GetNextFragmentInfo(nextTs, nextDur))
  {
    adaptiveTree_->SetFragmentDuration(
        stream.m_kodiAdStream.getAdaptationSet(), stream.m_kodiAdStream.getRepresentation(),
        stream.m_kodiAdStream.getSegmentPos(), nextTs, static_cast<uint32_t>(nextDur),
        streamReader->GetTimeScale());
  }
  stream.segmentChanged = false;
}

const AP4_UI08* Session::GetDefaultKeyId(const uint16_t index) const
{
  static const AP4_UI08 default_key[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  if (adaptiveTree_->current_period_->psshSets_[index].defaultKID_.size() == 16)
    return reinterpret_cast<const AP4_UI08*>(
        adaptiveTree_->current_period_->psshSets_[index].defaultKID_.data());
  return default_key;
}

Adaptive_CencSingleSampleDecrypter* Session::GetSingleSampleDecrypter(std::string sessionId)
{
  for (std::vector<CDMSESSION>::iterator b(cdm_sessions_.begin() + 1), e(cdm_sessions_.end());
       b != e; ++b)
    if (b->cdm_session_str_ && sessionId == b->cdm_session_str_)
      return b->single_sample_decryptor_;
  return nullptr;
}

uint32_t Session::GetIncludedStreamMask() const
{
  const INPUTSTREAM_TYPE adp2ips[] = {
      INPUTSTREAM_TYPE_NONE, INPUTSTREAM_TYPE_VIDEO, INPUTSTREAM_TYPE_AUDIO,
      INPUTSTREAM_TYPE_SUBTITLE};
  uint32_t res(0);
  for (unsigned int i(0); i < 4; ++i)
    if (adaptiveTree_->current_period_->included_types_ & (1U << i))
      res |= (1U << adp2ips[i]);
  return res;
}

STREAM_CRYPTO_KEY_SYSTEM Session::GetCryptoKeySystem() const
{
  if (m_kodiProps.m_licenseType == "com.widevine.alpha")
    return STREAM_CRYPTO_KEY_SYSTEM_WIDEVINE;
#if STREAMCRYPTO_VERSION_LEVEL >= 1
  else if (m_kodiProps.m_licenseType == "com.huawei.wiseplay")
    return STREAM_CRYPTO_KEY_SYSTEM_WISEPLAY;
#endif
  else if (m_kodiProps.m_licenseType == "com.microsoft.playready")
    return STREAM_CRYPTO_KEY_SYSTEM_PLAYREADY;
  else
    return STREAM_CRYPTO_KEY_SYSTEM_NONE;
}

int Session::GetChapter() const
{
  if (adaptiveTree_)
  {
    std::vector<adaptive::AdaptiveTree::Period*>::const_iterator res =
        std::find(adaptiveTree_->periods_.cbegin(), adaptiveTree_->periods_.cend(),
                  adaptiveTree_->current_period_);
    if (res != adaptiveTree_->periods_.cend())
      return (res - adaptiveTree_->periods_.cbegin()) + 1;
  }
  return -1;
}

int Session::GetChapterCount() const
{
  if (adaptiveTree_)
    return adaptiveTree_->periods_.size() > 1 ? adaptiveTree_->periods_.size() : 0;
  return 0;
}

const char* Session::GetChapterName(int ch) const
{
  --ch;
  if (ch >= 0 && ch < static_cast<int>(adaptiveTree_->periods_.size()))
    return adaptiveTree_->periods_[ch]->id_.c_str();
  return "[Unknown]";
}

int64_t Session::GetChapterPos(int ch) const
{
  int64_t sum(0);
  --ch;

  for (; ch; --ch)
    sum += (adaptiveTree_->periods_[ch - 1]->duration_ * STREAM_TIME_BASE) /
           adaptiveTree_->periods_[ch - 1]->timescale_;
  return sum / STREAM_TIME_BASE;
}

uint64_t Session::GetChapterStartTime() const
{
  uint64_t start_time = 0;
  for (adaptive::AdaptiveTree::Period* p : adaptiveTree_->periods_)
    if (p == adaptiveTree_->current_period_)
      break;
    else
      start_time += (p->duration_ * STREAM_TIME_BASE) / p->timescale_;
  return start_time;
}

int Session::GetPeriodId() const
{
  if (adaptiveTree_)
  {
    if (IsLive())
      return adaptiveTree_->current_period_->sequence_ == adaptiveTree_->initial_sequence_
                 ? 1
                 : adaptiveTree_->current_period_->sequence_ + 1;
    else
      return GetChapter();
  }
  return -1;
}

bool Session::SeekChapter(int ch)
{
  if (adaptiveTree_->next_period_)
    return true;

  --ch;
  if (ch >= 0 && ch < static_cast<int>(adaptiveTree_->periods_.size()) &&
      adaptiveTree_->periods_[ch] != adaptiveTree_->current_period_)
  {
    adaptiveTree_->next_period_ = adaptiveTree_->periods_[ch];
    for (auto& stream : m_streams)
    {
      if (stream->GetReader())
        stream->GetReader()->Reset(true);
    }
    return true;
  }

  return false;
}

/***************************  Interface *********************************/

class CInputStreamAdaptive;

/*******************************************************/
/*                     VideoCodec                      */
/*******************************************************/

class ATTR_DLL_LOCAL CVideoCodecAdaptive : public kodi::addon::CInstanceVideoCodec
{
public:
  CVideoCodecAdaptive(const kodi::addon::IInstanceInfo& instance);
  CVideoCodecAdaptive(const kodi::addon::IInstanceInfo& instance, CInputStreamAdaptive* parent);
  virtual ~CVideoCodecAdaptive();

  bool Open(const kodi::addon::VideoCodecInitdata& initData) override;
  bool Reconfigure(const kodi::addon::VideoCodecInitdata& initData) override;
  bool AddData(const DEMUX_PACKET& packet) override;
  VIDEOCODEC_RETVAL GetPicture(VIDEOCODEC_PICTURE& picture) override;
  const char* GetName() override { return m_name.c_str(); };
  void Reset() override;

private:
  enum STATE : unsigned int
  {
    STATE_WAIT_EXTRADATA = 1
  };

  std::shared_ptr<Session> m_session;
  unsigned int m_state;
  std::string m_name;
};

/*******************************************************/
/*                     InputStream                     */
/*******************************************************/

class ATTR_DLL_LOCAL CInputStreamAdaptive : public kodi::addon::CInstanceInputStream
{
public:
  CInputStreamAdaptive(const kodi::addon::IInstanceInfo& instance);
  ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo& instance,
                              KODI_ADDON_INSTANCE_HDL& hdl) override;

  bool Open(const kodi::addon::InputstreamProperty& props) override;
  void Close() override;
  bool GetStreamIds(std::vector<unsigned int>& ids) override;
  void GetCapabilities(kodi::addon::InputstreamCapabilities& caps) override;
  bool GetStream(int streamid, kodi::addon::InputstreamInfo& info) override;
  void EnableStream(int streamid, bool enable) override;
  bool OpenStream(int streamid) override;
  DEMUX_PACKET* DemuxRead() override;
  bool DemuxSeekTime(double time, bool backwards, double& startpts) override;
  void SetVideoResolution(int width, int height) override;
  bool PosTime(int ms) override;
  int GetTotalTime() override;
  int GetTime() override;
  bool IsRealTimeStream() override;

#if INPUTSTREAM_VERSION_LEVEL > 1
  int GetChapter() override;
  int GetChapterCount() override;
  const char* GetChapterName(int ch) override;
  int64_t GetChapterPos(int ch) override;
  bool SeekChapter(int ch) override;
#endif

  std::shared_ptr<Session> GetSession() { return m_session; };

private:
  std::shared_ptr<Session> m_session;
  UTILS::PROPERTIES::KodiProperties m_kodiProps;
  int m_width, m_height;
  uint32_t m_IncludedStreams[16];
  bool m_checkChapterSeek = false;
  int m_failedSeekTime = ~0;

  void UnlinkIncludedStreams(Session::STREAM* stream);
};

CInputStreamAdaptive::CInputStreamAdaptive(const kodi::addon::IInstanceInfo& instance)
  : CInstanceInputStream(instance), m_session(nullptr), m_width(1280), m_height(720)
{
  memset(m_IncludedStreams, 0, sizeof(m_IncludedStreams));
}

ADDON_STATUS CInputStreamAdaptive::CreateInstance(const kodi::addon::IInstanceInfo& instance,
                                                  KODI_ADDON_INSTANCE_HDL& hdl)
{
  if (instance.IsType(ADDON_INSTANCE_VIDEOCODEC))
  {
    hdl = new CVideoCodecAdaptive(instance, this);
    return ADDON_STATUS_OK;
  }
  return ADDON_STATUS_NOT_IMPLEMENTED;
}

bool CInputStreamAdaptive::Open(const kodi::addon::InputstreamProperty& props)
{
  LOG::Log(LOGDEBUG, "Open()");

  std::string url = props.GetURL();
  m_kodiProps = PROPERTIES::ParseKodiProperties(props.GetProperties());

  if (m_kodiProps.m_manifestType == PROPERTIES::ManifestType::UNKNOWN)
    return false;

  std::uint8_t drmConfig{0};
  if (m_kodiProps.m_isLicensePersistentStorage)
    drmConfig |= SSD::SSD_DECRYPTER::CONFIG_PERSISTENTSTORAGE;

  std::map<std::string, std::string> mediaHeaders{m_kodiProps.m_streamHeaders};

  // If the URL contains headers then replace the stream headers
  std::string::size_type posHeader(url.find("|"));
  if (posHeader != std::string::npos)
  {
    m_kodiProps.m_streamHeaders.clear();
    ParseHeaderString(m_kodiProps.m_streamHeaders, url.substr(posHeader + 1));
    url = url.substr(0, posHeader);
  }
  if (mediaHeaders.empty())
    mediaHeaders = m_kodiProps.m_streamHeaders;

  kodihost->SetProfilePath(props.GetProfileFolder());

  m_session = std::make_shared<Session>(m_kodiProps, url, mediaHeaders, props.GetProfileFolder());
  m_session->SetVideoResolution(m_width, m_height);

  if (!m_session->Initialize(drmConfig, m_kodiProps.m_bandwidthMax))
  {
    m_session = nullptr;
    return false;
  }
  return true;
}

void CInputStreamAdaptive::Close(void)
{
  LOG::Log(LOGDEBUG, "Close()");
  m_session = nullptr;
}

bool CInputStreamAdaptive::GetStreamIds(std::vector<unsigned int>& ids)
{
  LOG::Log(LOGDEBUG, "GetStreamIds()");
  INPUTSTREAM_IDS iids;

  if (m_session)
  {
    adaptive::AdaptiveTree::Period* period;
    int period_id = m_session->GetPeriodId();
    iids.m_streamCount = 0;
    unsigned int id;

    for (unsigned int i(1); i <= INPUTSTREAM_MAX_STREAM_COUNT && i <= m_session->GetStreamCount();
         ++i)
    {
      Session::STREAM* stream = m_session->GetStream(i);
      if (!stream)
      {
        LOG::LogF(LOGERROR, "Cannot get the stream from sid %u", i);
        continue;
      }

      uint8_t cdmId(static_cast<uint8_t>(stream->m_kodiAdStream.getRepresentation()->pssh_set_));
      if (stream->valid &&
          (m_session->GetMediaTypeMask() & static_cast<uint8_t>(1) << stream->m_kodiAdStream.get_type()))
      {
        if (m_session->GetMediaTypeMask() != 0xFF)
        {
          const adaptive::AdaptiveTree::Representation* rep(stream->m_kodiAdStream.getRepresentation());
          if (rep->flags_ & adaptive::AdaptiveTree::Representation::INCLUDEDSTREAM)
            continue;
        }
        if (m_session->IsLive())
        {
          period = stream->m_kodiAdStream.getPeriod();
          if (period->sequence_ == m_session->GetInitialSequence())
          {
            id = i + 1000;
          }
          else
          {
            id = i + (period->sequence_ + 1) * 1000;
          }
        }
        else
        {
          id = i + period_id * 1000;
        }
        ids.emplace_back(id);
      }
    }
  }

  return !ids.empty();
}

void CInputStreamAdaptive::GetCapabilities(kodi::addon::InputstreamCapabilities& caps)
{
  LOG::Log(LOGDEBUG, "GetCapabilities()");
  uint32_t mask = INPUTSTREAM_SUPPORTS_IDEMUX | INPUTSTREAM_SUPPORTS_IDISPLAYTIME |
                  INPUTSTREAM_SUPPORTS_IPOSTIME | INPUTSTREAM_SUPPORTS_SEEK |
                  INPUTSTREAM_SUPPORTS_PAUSE;
#if INPUTSTREAM_VERSION_LEVEL > 1
  mask |= INPUTSTREAM_SUPPORTS_ICHAPTER;
#endif
  caps.SetMask(mask);
}

bool CInputStreamAdaptive::GetStream(int streamid, kodi::addon::InputstreamInfo& info)
{
  LOG::Log(LOGDEBUG, "GetStream(%d)", streamid);

  Session::STREAM* stream(m_session->GetStream(streamid - m_session->GetPeriodId() * 1000));

  if (stream)
  {
    uint8_t cdmId(static_cast<uint8_t>(stream->m_kodiAdStream.getRepresentation()->pssh_set_));
    if (stream->encrypted && m_session->GetCDMSession(cdmId) != nullptr)
    {
      kodi::addon::StreamCryptoSession cryptoSession;

      LOG::Log(LOGDEBUG, "GetStream(%d): initalizing crypto session", streamid);
      cryptoSession.SetKeySystem(m_session->GetCryptoKeySystem());

      const char* sessionId(m_session->GetCDMSession(cdmId));
      cryptoSession.SetSessionId(sessionId);

      if (m_session->GetDecrypterCaps(cdmId).flags &
          SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SUPPORTS_DECODING)
        stream->info_.SetFeatures(INPUTSTREAM_FEATURE_DECODE);
      else
        stream->info_.SetFeatures(0);

      cryptoSession.SetFlags((m_session->GetDecrypterCaps(cdmId).flags &
                          SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_DECODER)
                             ? STREAM_CRYPTO_FLAG_SECURE_DECODER
                             : 0);
      stream->info_.SetCryptoSession(cryptoSession);
    }

    info = stream->info_;
    return true;
  }

  return false;
}

void CInputStreamAdaptive::UnlinkIncludedStreams(Session::STREAM* stream)
{
  if (stream->mainId_)
  {
    Session::STREAM* mainStream(m_session->GetStream(stream->mainId_));
    if (mainStream->GetReader())
      mainStream->GetReader()->RemoveStreamType(stream->info_.GetStreamType());
  }
  const adaptive::AdaptiveTree::Representation* rep(stream->m_kodiAdStream.getRepresentation());
  if (rep->flags_ & adaptive::AdaptiveTree::Representation::INCLUDEDSTREAM)
    m_IncludedStreams[stream->info_.GetStreamType()] = 0;
}

void CInputStreamAdaptive::EnableStream(int streamid, bool enable)
{
  LOG::Log(LOGDEBUG, "EnableStream(%d: %s)", streamid, enable ? "true" : "false");

  if (!m_session)
    return;

  Session::STREAM* stream(m_session->GetStream(streamid - m_session->GetPeriodId() * 1000));

  if (!enable && stream && stream->enabled)
  {
    UnlinkIncludedStreams(stream);
    m_session->EnableStream(stream, false);
  }
}

// We call true if a reset is required, otherwise false.
bool CInputStreamAdaptive::OpenStream(int streamid)
{
  LOG::Log(LOGDEBUG, "OpenStream(%d)", streamid);

  if (!m_session)
    return false;

  Session::STREAM* stream(m_session->GetStream(streamid - m_session->GetPeriodId() * 1000));

  if (!stream)
    return false;

  if (stream->enabled)
  {
    if (stream->m_kodiAdStream.StreamChanged())
    {
      UnlinkIncludedStreams(stream);
      stream->reset();
      stream->m_kodiAdStream.Reset();
    }
    else
      return false;
  }

  bool needRefetch = false; //Make sure that Kodi fetches changes
  stream->enabled = true;

  const adaptive::AdaptiveTree::Representation* rep(stream->m_kodiAdStream.getRepresentation());

  // If we select a dummy (=inside video) stream, open the video part
  // Dummy streams will be never enabled, they will only enable / activate audio track.
  if (rep->flags_ & adaptive::AdaptiveTree::Representation::INCLUDEDSTREAM)
  {
    Session::STREAM* mainStream;
    stream->mainId_ = 0;
    while ((mainStream = m_session->GetStream(++stream->mainId_)))
      if (mainStream->info_.GetStreamType() == INPUTSTREAM_TYPE_VIDEO && mainStream->enabled)
        break;
    if (mainStream)
    {
      SampleReader* mainReader = mainStream->GetReader();
      if (!mainReader)
      {
        LOG::LogF(LOGERROR, "Cannot get the stream sample reader");
      }
      else
      {
        mainReader->AddStreamType(stream->info_.GetStreamType(), streamid);
        mainReader->GetInformation(stream->info_);
      }
    }
    else
    {
      stream->mainId_ = 0;
    }
    m_IncludedStreams[stream->info_.GetStreamType()] = streamid;
    return false;
  }

  if (rep->flags_ & adaptive::AdaptiveTree::Representation::SUBTITLESTREAM)
  {
    stream->SetReader(std::make_unique<SubtitleSampleReader>(
        rep->url_, streamid, stream->info_.GetCodecInternalName()));
    return false;
  }

  AP4_Movie* movie(m_session->PrepareStream(stream, needRefetch));

  // We load fragments on PrepareTime for HLS manifests and have to reevaluate the start-segment
  //if (m_session->GetManifestType() == PROPERTIES::ManifestType::HLS)
  //  stream->m_kodiAdStream.restart_stream();
  stream->m_kodiAdStream.start_stream();

  if (rep->containerType_ == adaptive::AdaptiveTree::CONTAINERTYPE_TEXT)
  {
    stream->SetAdByteStream(std::make_unique<AdaptiveByteStream>(&stream->m_kodiAdStream));
    stream->SetReader(std::make_unique<SubtitleSampleReader>(stream, streamid,
                                                             stream->info_.GetCodecInternalName()));
  }
  else if (rep->containerType_ == adaptive::AdaptiveTree::CONTAINERTYPE_TS)
  {
    stream->SetAdByteStream(std::make_unique<AdaptiveByteStream>(&stream->m_kodiAdStream));

    uint32_t mask{(1U << stream->info_.GetStreamType()) | m_session->GetIncludedStreamMask()};
    stream->SetReader(std::make_unique<TSSampleReader>(
        stream->GetAdByteStream(), stream->info_.GetStreamType(), streamid, mask));

    if (!stream->GetReader()->Initialize())
    {
      stream->disable();
      return false;
    }
    m_session->OnSegmentChanged(&stream->m_kodiAdStream);
  }
  else if (rep->containerType_ == adaptive::AdaptiveTree::CONTAINERTYPE_ADTS)
  {
    stream->SetAdByteStream(std::make_unique<AdaptiveByteStream>(&stream->m_kodiAdStream));
    stream->SetReader(std::make_unique<ADTSSampleReader>(stream->GetAdByteStream(), streamid));
  }
  else if (rep->containerType_ == adaptive::AdaptiveTree::CONTAINERTYPE_WEBM)
  {
    stream->SetAdByteStream(std::make_unique<AdaptiveByteStream>(&stream->m_kodiAdStream));
    stream->SetReader(std::make_unique<WebmSampleReader>(stream->GetAdByteStream(), streamid));
    if (!stream->GetReader()->Initialize())
    {
      stream->disable();
      return false;
    }
  }
  else if (rep->containerType_ == adaptive::AdaptiveTree::CONTAINERTYPE_MP4)
  {
    stream->SetAdByteStream(std::make_unique<AdaptiveByteStream>(&stream->m_kodiAdStream));
    stream->SetStreamFile(std::make_unique<AP4_File>(
        *stream->GetAdByteStream(), AP4_DefaultAtomFactory::Instance_, true, movie));
    movie = stream->GetStreamFile()->GetMovie();

    if (movie == NULL)
    {
      LOG::Log(LOGERROR, "No MOOV in stream!");
      m_session->EnableStream(stream, false);
      return false;
    }

    AP4_Track* track = movie->GetTrack(TIDC[stream->m_kodiAdStream.get_type()]);
    if (!track)
    {
      if (stream->m_kodiAdStream.get_type() == adaptive::AdaptiveTree::SUBTITLE)
        track = movie->GetTrack(AP4_Track::TYPE_TEXT);
      if (!track)
      {
        LOG::Log(LOGERROR, "No suitable track found in stream");
        m_session->EnableStream(stream, false);
        return false;
      }
    }

    auto sampleDecrypter =
        m_session->GetSingleSampleDecryptor(stream->m_kodiAdStream.getRepresentation()->pssh_set_);
    auto caps = m_session->GetDecrypterCaps(stream->m_kodiAdStream.getRepresentation()->pssh_set_);

    stream->SetReader(std::make_unique<FragmentedSampleReader>(
        stream->GetAdByteStream(), movie, track, streamid, sampleDecrypter, caps));
  }
  else
  {
    m_session->EnableStream(stream, false);
    return false;
  }

  if (stream->info_.GetStreamType() == INPUTSTREAM_TYPE_VIDEO)
  {
    for (uint16_t i(0); i < 16; ++i)
    {
      if (m_IncludedStreams[i])
      {
        stream->GetReader()->AddStreamType(static_cast<INPUTSTREAM_TYPE>(i), m_IncludedStreams[i]);
        unsigned int sid = m_IncludedStreams[i] - m_session->GetPeriodId() * 1000;
        Session::STREAM* incStream = m_session->GetStream(sid);
        if (!incStream) {
          LOG::LogF(LOGERROR, "Cannot get the stream from sid %u", sid);
        }
        stream->GetReader()->GetInformation(
            m_session->GetStream(m_IncludedStreams[i] - m_session->GetPeriodId() * 1000)->info_);
      }
    }
  }
  m_session->EnableStream(stream, true);
  return stream->GetReader()->GetInformation(stream->info_) || needRefetch;
}


DEMUX_PACKET* CInputStreamAdaptive::DemuxRead(void)
{
  if (!m_session)
    return NULL;

  if (m_checkChapterSeek)
  {
    m_checkChapterSeek = false;
    if (m_session->GetChapterSeekTime() > 0)
    {
      m_session->SeekTime(m_session->GetChapterSeekTime());
      m_session->ResetChapterSeekTime();
    }
  }

  if (~m_failedSeekTime)
  {
    LOG::Log(LOGDEBUG, "Seeking to last failed seek position (%d)", m_failedSeekTime);
    m_session->SeekTime(static_cast<double>(m_failedSeekTime) * 0.001f, 0, false);
    m_failedSeekTime = ~0;
  }

  SampleReader* sr(m_session->GetNextSample());

  if (m_session->CheckChange())
  {
    DEMUX_PACKET* p = AllocateDemuxPacket(0);
    p->iStreamId = DEMUX_SPECIALID_STREAMCHANGE;
    LOG::Log(LOGDEBUG, "DEMUX_SPECIALID_STREAMCHANGE");
    return p;
  }

  if (sr)
  {
    AP4_Size iSize(sr->GetSampleDataSize());
    const AP4_UI08* pData(sr->GetSampleData());
    DEMUX_PACKET* p;

    if (sr->IsEncrypted() && iSize > 0 && pData)
    {
      unsigned int numSubSamples(*((unsigned int*)pData));
      pData += sizeof(numSubSamples);
      p = AllocateEncryptedDemuxPacket(iSize, numSubSamples);
      memcpy(p->cryptoInfo->clearBytes, pData, numSubSamples * sizeof(uint16_t));
      pData += (numSubSamples * sizeof(uint16_t));
      memcpy(p->cryptoInfo->cipherBytes, pData, numSubSamples * sizeof(uint32_t));
      pData += (numSubSamples * sizeof(uint32_t));
      memcpy(p->cryptoInfo->iv, pData, 16);
      pData += 16;
      memcpy(p->cryptoInfo->kid, pData, 16);
      pData += 16;
      iSize -= (pData - sr->GetSampleData());
      p->cryptoInfo->flags = 0;
    }
    else
      p = AllocateDemuxPacket(iSize);

    if (iSize > 0 && pData)
    {
      p->dts = static_cast<double>(sr->DTS() + m_session->GetChapterStartTime());
      p->pts = static_cast<double>(sr->PTS() + m_session->GetChapterStartTime());
      p->duration = static_cast<double>(sr->GetDuration());
      p->iStreamId = sr->GetStreamId();
      p->iGroupId = 0;
      p->iSize = iSize;
      memcpy(p->pData, pData, iSize);
    }

    //LOG::Log(LOGDEBUG, "DTS: %0.4f, PTS:%0.4f, ID: %u SZ: %d", p->dts, p->pts, p->iStreamId, p->iSize);

    sr->ReadSample();
    return p;
  }

  if (m_session->SeekChapter(m_session->GetChapter() + 1))
  {
    m_checkChapterSeek = true;
    for (unsigned int i(1);
         i <= INPUTSTREAM_MAX_STREAM_COUNT && i <= m_session->GetStreamCount(); ++i)
      EnableStream(i + m_session->GetPeriodId() * 1000, false);
    m_session->InitializePeriod();
    DEMUX_PACKET* p = AllocateDemuxPacket(0);
    p->iStreamId = DEMUX_SPECIALID_STREAMCHANGE;
    LOG::Log(LOGDEBUG, "DEMUX_SPECIALID_STREAMCHANGE");
    return p;
  }
  return NULL;
}

// Accurate search (PTS based)
bool CInputStreamAdaptive::DemuxSeekTime(double time, bool backwards, double& startpts)
{
  return true;
}

//callback - will be called from kodi
void CInputStreamAdaptive::SetVideoResolution (int width, int height)
{
  LOG::Log(LOGINFO, "SetVideoResolution (%d x %d)", width, height);
  if (m_session)
    m_session->SetVideoResolution(width, height);
  else
  {
    m_width = width;
    m_height = height;
  }
}

bool CInputStreamAdaptive::PosTime(int ms)
{
  if (!m_session)
    return false;

  LOG::Log(LOGINFO, "PosTime (%d)", ms);

  bool ret = m_session->SeekTime(static_cast<double>(ms) * 0.001f, 0, false);
  m_failedSeekTime = ret ? ~0 : ms;

  return ret;
}

int CInputStreamAdaptive::GetTotalTime()
{
  if (!m_session)
    return 0;

  return static_cast<int>(m_session->GetTotalTimeMs());
}

int CInputStreamAdaptive::GetTime()
{
  if (!m_session)
    return 0;

  int timeMs = static_cast<int>(m_session->GetElapsedTimeMs());
  return timeMs;
}

bool CInputStreamAdaptive::IsRealTimeStream()
{
  return m_session && m_session->IsLive();
}

#if INPUTSTREAM_VERSION_LEVEL > 1
int CInputStreamAdaptive::GetChapter()
{
  return m_session ? m_session->GetChapter() : 0;
}

int CInputStreamAdaptive::GetChapterCount()
{
  return m_session ? m_session->GetChapterCount() : 0;
}

const char* CInputStreamAdaptive::GetChapterName(int ch)
{
  return m_session ? m_session->GetChapterName(ch) : 0;
}

int64_t CInputStreamAdaptive::GetChapterPos(int ch)
{
  return m_session ? m_session->GetChapterPos(ch) : 0;
}

bool CInputStreamAdaptive::SeekChapter(int ch)
{
  return m_session ? m_session->SeekChapter(ch) : false;
}
#endif
/*****************************************************************************************************/

CVideoCodecAdaptive::CVideoCodecAdaptive(const kodi::addon::IInstanceInfo& instance)
  : CInstanceVideoCodec(instance),
    m_session(nullptr),
    m_state(0),
    m_name("inputstream.adaptive.decoder")
{
}

CVideoCodecAdaptive::CVideoCodecAdaptive(const kodi::addon::IInstanceInfo& instance,
                                         CInputStreamAdaptive* parent)
  : CInstanceVideoCodec(instance), m_session(parent->GetSession()), m_state(0)
{
}

CVideoCodecAdaptive::~CVideoCodecAdaptive()
{
}

bool CVideoCodecAdaptive::Open(const kodi::addon::VideoCodecInitdata& initData)
{
  if (!m_session || !m_session->GetDecrypter())
    return false;

  if (initData.GetCodecType() == VIDEOCODEC_H264 && !initData.GetExtraDataSize() &&
      !(m_state & STATE_WAIT_EXTRADATA))
  {
    LOG::Log(LOGINFO, "VideoCodec::Open: Wait ExtraData");
    m_state |= STATE_WAIT_EXTRADATA;
    return true;
  }
  m_state &= ~STATE_WAIT_EXTRADATA;

  LOG::Log(LOGINFO, "VideoCodec::Open");

  m_name = "inputstream.adaptive";
  switch (initData.GetCodecType())
  {
    case VIDEOCODEC_VP8:
      m_name += ".vp8";
      break;
    case VIDEOCODEC_H264:
      m_name += ".h264";
      break;
    case VIDEOCODEC_VP9:
      m_name += ".vp9";
      break;
    default:;
  }
  m_name += ".decoder";

  std::string sessionId(initData.GetCryptoSession().GetSessionId());
  Adaptive_CencSingleSampleDecrypter* ssd(m_session->GetSingleSampleDecrypter(sessionId));

  return m_session->GetDecrypter()->OpenVideoDecoder(
      ssd, reinterpret_cast<const SSD::SSD_VIDEOINITDATA*>(initData.GetCStructure()));
}

bool CVideoCodecAdaptive::Reconfigure(const kodi::addon::VideoCodecInitdata& initData)
{
  return false;
}

bool CVideoCodecAdaptive::AddData(const DEMUX_PACKET& packet)
{
  if (!m_session || !m_session->GetDecrypter())
    return false;

  SSD::SSD_SAMPLE sample;
  sample.data = packet.pData;
  sample.dataSize = packet.iSize;
  sample.flags = 0;
  sample.pts = (int64_t)packet.pts;
  if (packet.cryptoInfo)
  {
    sample.numSubSamples = packet.cryptoInfo->numSubSamples;
    sample.clearBytes = packet.cryptoInfo->clearBytes;
    sample.cipherBytes = packet.cryptoInfo->cipherBytes;
    sample.iv = packet.cryptoInfo->iv;
    sample.kid = packet.cryptoInfo->kid;
  }
  else
  {
    sample.numSubSamples = 0;
    sample.iv = sample.kid = nullptr;
  }

  return m_session->GetDecrypter()->DecodeVideo(
             dynamic_cast<kodi::addon::CInstanceVideoCodec*>(this), &sample, nullptr) !=
         SSD::VC_ERROR;
}

VIDEOCODEC_RETVAL CVideoCodecAdaptive::GetPicture(VIDEOCODEC_PICTURE& picture)
{
  if (!m_session || !m_session->GetDecrypter())
    return VIDEOCODEC_RETVAL::VC_ERROR;

  static VIDEOCODEC_RETVAL vrvm[] = {VIDEOCODEC_RETVAL::VC_NONE, VIDEOCODEC_RETVAL::VC_ERROR,
                                     VIDEOCODEC_RETVAL::VC_BUFFER, VIDEOCODEC_RETVAL::VC_PICTURE,
                                     VIDEOCODEC_RETVAL::VC_EOF};

  return vrvm[m_session->GetDecrypter()->DecodeVideo(
      dynamic_cast<kodi::addon::CInstanceVideoCodec*>(this), nullptr,
      reinterpret_cast<SSD::SSD_PICTURE*>(&picture))];
}

void CVideoCodecAdaptive::Reset()
{
  if (!m_session || !m_session->GetDecrypter())
    return;

  m_session->GetDecrypter()->ResetVideo();
}

/*****************************************************************************************************/

class ATTR_DLL_LOCAL CMyAddon : public kodi::addon::CAddonBase
{
public:
  CMyAddon();
  virtual ~CMyAddon();
  ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo& instance,
                              KODI_ADDON_INSTANCE_HDL& hdl) override;
};

CMyAddon::CMyAddon()
{
  kodihost = nullptr;
  ;
}

CMyAddon::~CMyAddon()
{
  delete kodihost;
}

ADDON_STATUS CMyAddon::CreateInstance(const kodi::addon::IInstanceInfo& instance,
                                      KODI_ADDON_INSTANCE_HDL& hdl)
{
  if (instance.IsType(ADDON_INSTANCE_INPUTSTREAM))
  {
    hdl = new CInputStreamAdaptive(instance);
    kodihost = new KodiHost();
    return ADDON_STATUS_OK;
  }
  return ADDON_STATUS_NOT_IMPLEMENTED;
}

ADDONCREATOR(CMyAddon);
