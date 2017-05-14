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

#include "main.h"

#include <iostream>
#include <stdio.h>
#include <string.h>
#include <sstream>

#include "xbmc_addon_types.h"
#include "libXBMC_addon.h"
#include "helpers.h"
#include "kodi_vfs_types.h"
#include "SSD_dll.h"
#include "parser/DASHTree.h"
#include "parser/SmoothTree.h"

#define SAFE_DELETE(p)       do { delete (p);     (p)=NULL; } while (0)

ADDON::CHelper_libXBMC_addon *xbmc = 0;
std::uint16_t kodiDisplayWidth(0), kodiDisplayHeight(0);

/*******************************************************
kodi host - interface for decrypter libraries
********************************************************/
class KodiHost : public SSD_HOST
{
public:
  virtual const char *GetLibraryPath() const override
  {
    return m_strLibraryPath.c_str();
  };

  virtual const char *GetProfilePath() const override
  {
    return m_strProfilePath.c_str();
  };

  virtual void* CURLCreate(const char* strURL) override
  {
    return xbmc->CURLCreate(strURL);
  };

  virtual bool CURLAddOption(void* file, CURLOPTIONS opt, const char* name, const char * value)override
  {
    const XFILE::CURLOPTIONTYPE xbmcmap[] = {XFILE::CURL_OPTION_PROTOCOL, XFILE::CURL_OPTION_HEADER};
    return xbmc->CURLAddOption(file, xbmcmap[opt], name, value);
  }

  virtual bool CURLOpen(void* file)override
  {
    return xbmc->CURLOpen(file, XFILE::READ_NO_CACHE);
  };

  virtual size_t ReadFile(void* file, void* lpBuf, size_t uiBufSize)override
  {
    return xbmc->ReadFile(file, lpBuf, uiBufSize);
  };

  virtual void CloseFile(void* file)override
  {
    return xbmc->CloseFile(file);
  };

  virtual bool CreateDirectory(const char *dir)override
  {
    return xbmc->CreateDirectory(dir);
  };

  virtual void Log(LOGLEVEL level, const char *msg)override
  {
    const ADDON::addon_log_t xbmcmap[] = { ADDON::LOG_DEBUG, ADDON::LOG_INFO, ADDON::LOG_ERROR };
    return xbmc->Log(xbmcmap[level], msg);
  };

  void SetLibraryPath(const char *libraryPath)
  {
    m_strLibraryPath = libraryPath;

    const char *pathSep(libraryPath[0] && libraryPath[1] == ':' && isalpha(libraryPath[0]) ? "\\" : "/");

    if (m_strLibraryPath.size() && m_strLibraryPath.back() != pathSep[0])
      m_strLibraryPath += pathSep;
  }

  void SetProfilePath(const char *profilePath)
  {
    m_strProfilePath = profilePath;

    const char *pathSep(profilePath[0] && profilePath[1] == ':' && isalpha(profilePath[0]) ? "\\" : "/");

    if (m_strProfilePath.size() && m_strProfilePath.back() != pathSep[0])
      m_strProfilePath += pathSep;

    //let us make cdm userdata out of the addonpath and share them between addons
    m_strProfilePath.resize(m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 2));
    m_strProfilePath.resize(m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 1));
    m_strProfilePath.resize(m_strProfilePath.find_last_of(pathSep[0], m_strProfilePath.length() - 1) + 1);

    xbmc->CreateDirectory(m_strProfilePath.c_str());
    m_strProfilePath += "cdm";
    m_strProfilePath += pathSep;
    xbmc->CreateDirectory(m_strProfilePath.c_str());
  }

private:
  std::string m_strProfilePath, m_strLibraryPath;

}kodihost;

struct addonstring
{
    addonstring(char *d){data_= d;};
    ~addonstring() {xbmc->FreeString(data_);};
    const char* c_str() {return data_? data_:"";};
    char *data_;
};

/*******************************************************
Bento4 Streams
********************************************************/

class AP4_DASHStream : public AP4_ByteStream
{
public:
  // Constructor
  AP4_DASHStream(adaptive::AdaptiveStream *stream) :stream_(stream){};

  // AP4_ByteStream methods
  AP4_Result ReadPartial(void*    buffer,
    AP4_Size  bytesToRead,
    AP4_Size& bytesRead) override
  {
    bytesRead = stream_->read(buffer, bytesToRead);
    return bytesRead > 0 ? AP4_SUCCESS : AP4_ERROR_READ_FAILED;
  };
  AP4_Result WritePartial(const void* buffer,
    AP4_Size    bytesToWrite,
    AP4_Size&   bytesWritten) override
  {
    /* unimplemented */
    return AP4_ERROR_NOT_SUPPORTED;
  };
  AP4_Result Seek(AP4_Position position) override
  {
    return stream_->seek(position) ? AP4_SUCCESS : AP4_ERROR_NOT_SUPPORTED;
  };
  AP4_Result Tell(AP4_Position& position) override
  {
    position = stream_->tell();
    return AP4_SUCCESS;
  };
  AP4_Result GetSize(AP4_LargeSize& size) override
  {
    /* unimplemented */
    return AP4_ERROR_NOT_SUPPORTED;
  };
  // AP4_Referenceable methods
  void AddReference() override {};
  void Release()override      {};
protected:
  // members
  adaptive::AdaptiveStream *stream_;
};

/*******************************************************
Kodi Streams implementation
********************************************************/

bool adaptive::AdaptiveTree::download(const char* url, const std::map<std::string, std::string> &manifestHeaders)
{
  // open the file
  void* file = xbmc->CURLCreate(url);
  if (!file)
    return false;
  xbmc->CURLAddOption(file, XFILE::CURL_OPTION_PROTOCOL, "seekable", "0");
  xbmc->CURLAddOption(file, XFILE::CURL_OPTION_PROTOCOL, "acceptencoding", "gzip");
 
  for (const auto &entry : manifestHeaders)
  {
    xbmc->CURLAddOption(file, XFILE::CURL_OPTION_HEADER, entry.first.c_str(), entry.second.c_str());
  }

  xbmc->CURLOpen(file, XFILE::READ_CHUNKED | XFILE::READ_NO_CACHE);

  // read the file
  static const unsigned int CHUNKSIZE = 16384;
  char buf[CHUNKSIZE];
  size_t nbRead;
  while ((nbRead = xbmc->ReadFile(file, buf, CHUNKSIZE)) > 0 && ~nbRead && write_data(buf, nbRead));

  //download_speed_ = xbmc->GetFileDownloadSpeed(file);

  xbmc->CloseFile(file);

  xbmc->Log(ADDON::LOG_DEBUG, "Download %s finished", url);

  return nbRead == 0;
}

bool KodiAdaptiveStream::download(const char* url, const std::map<std::string, std::string> &mediaHeaders)
{
  // open the file
  void* file = xbmc->CURLCreate(url);
  if (!file)
    return false;
  xbmc->CURLAddOption(file, XFILE::CURL_OPTION_PROTOCOL, "seekable" , "0");
  xbmc->CURLAddOption(file, XFILE::CURL_OPTION_HEADER, "Connection", "keep-alive");
  xbmc->CURLAddOption(file, XFILE::CURL_OPTION_PROTOCOL, "acceptencoding", "gzip, deflate");
  
  for (const auto &entry : mediaHeaders)
  {
    xbmc->CURLAddOption(file, XFILE::CURL_OPTION_HEADER, entry.first.c_str(), entry.second.c_str());
  }

  xbmc->CURLOpen(file, XFILE::READ_CHUNKED | XFILE::READ_NO_CACHE | XFILE::READ_AUDIO_VIDEO);

  // read the file
  char *buf = (char*)malloc(1024*1024);
  size_t nbRead, nbReadOverall = 0;
  while ((nbRead = xbmc->ReadFile(file, buf, 1024 * 1024)) > 0 && ~nbRead && write_data(buf, nbRead)) nbReadOverall+= nbRead;
  free(buf);

  if (!nbReadOverall)
  {
    xbmc->Log(ADDON::LOG_ERROR, "Download %s doesn't provide any data: invalid", url);
    return false;
  }

  double current_download_speed_ = xbmc->GetFileDownloadSpeed(file);
  //Calculate the new downloadspeed to 1MB
  static const size_t ref_packet = 1024 * 1024;
  if (nbReadOverall >= ref_packet)
    set_download_speed(current_download_speed_);
  else
  {
    double ratio = (double)nbReadOverall / ref_packet;
    set_download_speed((get_download_speed() * (1.0 - ratio)) + current_download_speed_*ratio);
  }

  xbmc->CloseFile(file);

  xbmc->Log(ADDON::LOG_DEBUG, "Download %s finished, average download speed: %0.4lf", url, get_download_speed());

  return nbRead == 0;
}

bool KodiAdaptiveStream::parseIndexRange()
{
  // open the file
  xbmc->Log(ADDON::LOG_DEBUG, "Downloading %s for SIDX generation", getRepresentation()->url_.c_str());

  void* file = xbmc->CURLCreate(getRepresentation()->url_.c_str());
  if (!file)
    return false;
  xbmc->CURLAddOption(file, XFILE::CURL_OPTION_PROTOCOL, "seekable", "0");
  char rangebuf[64];
  sprintf(rangebuf, "bytes=%u-%u", getRepresentation()->indexRangeMin_, getRepresentation()->indexRangeMax_);
  xbmc->CURLAddOption(file, XFILE::CURL_OPTION_HEADER, "Range", rangebuf);
  if (!xbmc->CURLOpen(file, XFILE::READ_CHUNKED | XFILE::READ_NO_CACHE | XFILE::READ_AUDIO_VIDEO))
  {
    xbmc->Log(ADDON::LOG_ERROR, "Download SIDX retrieval failed");
    return false;
  }

  // read the file into AP4_MemoryByteStream
  AP4_MemoryByteStream byteStream;

  char buf[16384];
  size_t nbRead, nbReadOverall = 0;
  while ((nbRead = xbmc->ReadFile(file, buf, 16384)) > 0 && ~nbRead && AP4_SUCCEEDED(byteStream.Write(buf, nbRead))) nbReadOverall += nbRead;
  xbmc->CloseFile(file);

  if (nbReadOverall != getRepresentation()->indexRangeMax_ - getRepresentation()->indexRangeMin_ +1)
  {
    xbmc->Log(ADDON::LOG_ERROR, "Size of downloaded SIDX section differs from expected");
    return false;
  }
  byteStream.Seek(0);

  adaptive::AdaptiveTree::Representation *rep(const_cast<adaptive::AdaptiveTree::Representation*>(getRepresentation()));
  adaptive::AdaptiveTree::AdaptationSet *adp(const_cast<adaptive::AdaptiveTree::AdaptationSet*>(getAdaptationSet()));

  if (!getRepresentation()->indexRangeMin_)
  {
    AP4_File f(byteStream, AP4_DefaultAtomFactory::Instance, true);
    AP4_Movie* movie = f.GetMovie();
    if (movie == NULL)
    {
      xbmc->Log(ADDON::LOG_ERROR, "No MOOV in stream!");
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
    AP4_Atom *atom(NULL);
    if (AP4_FAILED(AP4_DefaultAtomFactory::Instance.CreateAtomFromStream(byteStream, atom)))
    {
      xbmc->Log(ADDON::LOG_ERROR, "Unable to create SIDX from IndexRange bytes");
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

    AP4_SidxAtom *sidx(AP4_DYNAMIC_CAST(AP4_SidxAtom, atom));
    const AP4_Array<AP4_SidxAtom::Reference> &refs(sidx->GetReferences());
    if (refs[0].m_ReferenceType == 1)
    {
      numSIDX = refs.ItemCount();
      delete atom;
      continue;
    }
    AP4_Position pos;
    byteStream.Tell(pos);
    seg.range_end_ = pos + getRepresentation()->indexRangeMin_ + sidx->GetFirstOffset() - 1;
    rep->timescale_ = sidx->GetTimeScale();

    for (unsigned int i(0); i < refs.ItemCount(); ++i)
    {
      seg.range_begin_ = seg.range_end_ + 1;
      seg.range_end_ = seg.range_begin_ + refs[i].m_ReferencedSize - 1;
      rep->segments_.data.push_back(seg);
      if (adp->segment_durations_.data.size() < rep->segments_.data.size() - 1)
        adp->segment_durations_.data.push_back(refs[i].m_SubsegmentDuration);
      seg.startPTS_ += refs[i].m_SubsegmentDuration;
    }
    delete atom;
    --numSIDX;
  } while (numSIDX);
  return true;
}

/*******************************************************
|   CodecHandler
********************************************************/

class CodecHandler
{
public:
  CodecHandler(AP4_SampleDescription *sd)
    : sample_description(sd)
    , extra_data(0)
    , extra_data_size(0)
    , naluLengthSize(0)
    , pictureId(0)
    , pictureIdPrev(0)
  {};
  virtual void UpdatePPSId(AP4_DataBuffer const&){};
  virtual bool GetVideoInformation(unsigned int &width, unsigned int &height){ return false; };
  virtual bool GetAudioInformation(unsigned int &channels){ return false; };

  AP4_SampleDescription *sample_description;
  const AP4_UI08 *extra_data;
  AP4_Size extra_data_size;
  AP4_UI08 naluLengthSize;
  AP4_UI08 pictureId, pictureIdPrev;
};

/***********************   AVC   ************************/

class AVCCodecHandler : public CodecHandler
{
public:
  AVCCodecHandler(AP4_SampleDescription *sd)
    : CodecHandler(sd)
    , countPictureSetIds(0)
    , needSliceInfo(false)
  {
    unsigned int width(0), height(0);
    if (AP4_VideoSampleDescription *video_sample_description = AP4_DYNAMIC_CAST(AP4_VideoSampleDescription, sample_description))
    {
      width = video_sample_description->GetWidth();
      height = video_sample_description->GetHeight();
    }
    if (AP4_AvcSampleDescription *avc = AP4_DYNAMIC_CAST(AP4_AvcSampleDescription, sample_description))
    {
      extra_data_size = avc->GetRawBytes().GetDataSize();
      extra_data = avc->GetRawBytes().GetData();
      countPictureSetIds = avc->GetPictureParameters().ItemCount();
      naluLengthSize = avc->GetNaluLengthSize();
      needSliceInfo = (countPictureSetIds > 1 || !width || !height);
    }
  }

  virtual void UpdatePPSId(AP4_DataBuffer const &buffer) override
  {
    if (!needSliceInfo)
      return;

    //Search the Slice header NALU
    const AP4_UI08 *data(buffer.GetData());
    unsigned int data_size(buffer.GetDataSize());
    for (; data_size;)
    {
      // sanity check
      if (data_size < naluLengthSize)
        break;

      // get the next NAL unit
      AP4_UI32 nalu_size;
      switch (naluLengthSize) {
      case 1:nalu_size = *data++; data_size--; break;
      case 2:nalu_size = AP4_BytesToInt16BE(data); data += 2; data_size -= 2; break;
      case 4:nalu_size = AP4_BytesToInt32BE(data); data += 4; data_size -= 4; break;
      default: data_size = 0; nalu_size = 1; break;
      }
      if (nalu_size > data_size)
        break;

      // Stop further NALU processing
      if (countPictureSetIds < 2)
        needSliceInfo = false;

      unsigned int nal_unit_type = *data & 0x1F;

      if (
        //nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_OF_NON_IDR_PICTURE ||
        nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_OF_IDR_PICTURE //||
        //nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_A ||
        //nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_B ||
        //nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_C
      ) {

        AP4_DataBuffer unescaped(data, data_size);
        AP4_NalParser::Unescape(unescaped);
        AP4_BitReader bits(unescaped.GetData(), unescaped.GetDataSize());

        bits.SkipBits(8); // NAL Unit Type

        AP4_AvcFrameParser::ReadGolomb(bits); // first_mb_in_slice
        AP4_AvcFrameParser::ReadGolomb(bits); // slice_type
        pictureId = AP4_AvcFrameParser::ReadGolomb(bits); //picture_set_id
      }
      // move to the next NAL unit
      data += nalu_size;
      data_size -= nalu_size;
    }
  }

  virtual bool GetVideoInformation(unsigned int &width, unsigned int &height) override
  {
    if (pictureId == pictureIdPrev)
      return false;
    pictureIdPrev = pictureId;

    if (AP4_AvcSampleDescription *avc = AP4_DYNAMIC_CAST(AP4_AvcSampleDescription, sample_description))
    {
      AP4_Array<AP4_DataBuffer>& buffer = avc->GetPictureParameters();
      AP4_AvcPictureParameterSet pps;
      for (unsigned int i(0); i < buffer.ItemCount(); ++i)
      {
        if (AP4_SUCCEEDED(AP4_AvcFrameParser::ParsePPS(buffer[i].GetData(), buffer[i].GetDataSize(), pps)) && pps.pic_parameter_set_id == pictureId)
        {
          buffer = avc->GetSequenceParameters();
          AP4_AvcSequenceParameterSet sps;
          for (unsigned int i(0); i < buffer.ItemCount(); ++i)
          {
            if (AP4_SUCCEEDED(AP4_AvcFrameParser::ParseSPS(buffer[i].GetData(), buffer[i].GetDataSize(), sps)) && sps.seq_parameter_set_id == pps.seq_parameter_set_id)
            {
              sps.GetInfo(width, height);
              return true;
            }
          }
          break;
        }
      }
    }
    return false;
  };
private:
  unsigned int countPictureSetIds;
  bool needSliceInfo;
};

/***********************   HEVC   ************************/

class HEVCCodecHandler : public CodecHandler
{
public:
  HEVCCodecHandler(AP4_SampleDescription *sd)
    :CodecHandler(sd)
  {
    if (AP4_HevcSampleDescription *hevc = AP4_DYNAMIC_CAST(AP4_HevcSampleDescription, sample_description))
    {
      extra_data_size = hevc->GetRawBytes().GetDataSize();
      extra_data = hevc->GetRawBytes().GetData();
      naluLengthSize = hevc->GetNaluLengthSize();
    }
  }
};

/***********************   MPEG   ************************/

class MPEGCodecHandler : public CodecHandler
{
public:
  MPEGCodecHandler(AP4_SampleDescription *sd)
    :CodecHandler(sd)
  {
    if (AP4_MpegSampleDescription *aac = AP4_DYNAMIC_CAST(AP4_MpegSampleDescription, sample_description))
    {
      extra_data_size = aac->GetDecoderInfo().GetDataSize();
      extra_data = aac->GetDecoderInfo().GetData();
    }
  }


  virtual bool GetAudioInformation(unsigned int &channels)
  {
    AP4_AudioSampleDescription *mpeg = AP4_DYNAMIC_CAST(AP4_AudioSampleDescription, sample_description);
    if (mpeg != nullptr && mpeg->GetChannelCount() != channels)
    {
      channels = mpeg->GetChannelCount();
      return true;
    }
    return false;
  }
};


/*******************************************************
|   FragmentedSampleReader
********************************************************/
class FragmentedSampleReader : public AP4_LinearReader
{
public:

  FragmentedSampleReader(AP4_ByteStream *input, AP4_Movie *movie, AP4_Track *track,
    AP4_UI32 streamId, AP4_CencSingleSampleDecrypter *ssd, const double pto)
    : AP4_LinearReader(*movie, input)
    , m_Track(track)
    , m_StreamId(streamId)
    , m_SampleDescIndex(0)
    , m_bSampleDescChanged(false)
    , m_fail_count_(0)
    , m_eos(false)
    , m_started(false)
    , m_dts(0.0)
    , m_pts(0.0)
    , m_presentationTimeOffset(pto)
    , m_codecHandler(0)
    , m_DefaultKey(0)
    , m_Protected_desc(0)
    , m_SingleSampleDecryptor(ssd)
    , m_Decrypter(0)
    , m_Observer(0)
  {
    EnableTrack(m_Track->GetId());

    AP4_SampleDescription *desc(m_Track->GetSampleDescription(0));
    if (desc->GetType() == AP4_SampleDescription::TYPE_PROTECTED)
      m_Protected_desc = static_cast<AP4_ProtectedSampleDescription*>(desc);
  }

  ~FragmentedSampleReader()
  {
    delete m_Decrypter;
    delete m_codecHandler;
  }

  AP4_Result Start(bool &bStarted)
  {
    bStarted = false;
    if (m_started)
      return AP4_SUCCESS;
    m_started = true;
    bStarted = true;
    return ReadSample();
  }

  AP4_Result ReadSample()
  {
    AP4_Result result;
    if (AP4_FAILED(result = ReadNextSample(m_Track->GetId(), m_sample_, m_Protected_desc ? m_encrypted : m_sample_data_)))
    {
      if (result == AP4_ERROR_EOS)
        m_eos = true;
      return result;
    }

    if (m_Protected_desc)
    {
      if (!m_Decrypter)
        return AP4_ERROR_EOS;

      // Make sure that the decrypter is NOT allocating memory!
      // If decrypter and addon are compiled with different DEBUG / RELEASE
      // options freeing HEAP memory will fail.
      m_sample_data_.Reserve(m_encrypted.GetDataSize() + 4096);
      m_SingleSampleDecryptor->SetFrameInfo(m_DefaultKey?16:0, m_DefaultKey, m_codecHandler->naluLengthSize);

      if (AP4_FAILED(result = m_Decrypter->DecryptSampleData(m_encrypted, m_sample_data_, NULL)))
      {
        xbmc->Log(ADDON::LOG_ERROR, "Decrypt Sample returns failure!");
        if (++m_fail_count_ > 50)
        {
          Reset(true);
          return result;
        }
        else
          m_sample_data_.SetDataSize(0);
      }
      else
        m_fail_count_ = 0;
    }

    m_dts = (double)m_sample_.GetDts() / (double)m_Track->GetMediaTimeScale() - m_presentationTimeOffset;
    m_pts = (double)m_sample_.GetCts() / (double)m_Track->GetMediaTimeScale() - m_presentationTimeOffset;

    m_codecHandler->UpdatePPSId(m_sample_data_);

    return AP4_SUCCESS;
  };

  void Reset(bool bEOS)
  {
    AP4_LinearReader::Reset();
    m_eos = bEOS;
  }

  bool EOS()const{ return m_eos; };
  double DTS()const{ return m_dts; };
  double PTS()const{ return m_pts; };
  const AP4_Sample &Sample()const { return m_sample_; };
  AP4_UI32 GetStreamId()const{ return m_StreamId; };
  AP4_Size GetSampleDataSize()const{ return m_sample_data_.GetDataSize(); };
  const AP4_Byte *GetSampleData()const{ return m_sample_data_.GetData(); };
  double GetDuration()const{ return (double)m_sample_.GetDuration() / (double)m_Track->GetMediaTimeScale(); };
  bool IsEncrypted() { return m_Protected_desc != nullptr; };
  bool GetInformation(INPUTSTREAM_INFO &info)
  {
    if (!m_codecHandler)
      return false;

    bool edchanged(false);

    if (m_bSampleDescChanged && info.m_ExtraSize != m_codecHandler->extra_data_size
      || memcmp(info.m_ExtraData, m_codecHandler->extra_data, m_codecHandler->extra_data_size))
    {
      free((void*)(info.m_ExtraData));
      info.m_ExtraSize = m_codecHandler->extra_data_size;
      info.m_ExtraData = (const uint8_t*)malloc(info.m_ExtraSize);
      memcpy((void*)info.m_ExtraData, m_codecHandler->extra_data, info.m_ExtraSize);
      edchanged = true;
    }

    m_bSampleDescChanged = false;

    if (m_codecHandler->GetVideoInformation(info.m_Width, info.m_Height)
      || m_codecHandler->GetAudioInformation(info.m_Channels))
      return true;

    return edchanged;
  }

  bool TimeSeek(double pts, bool preceeding)
  {
    AP4_Ordinal sampleIndex;
    if (AP4_SUCCEEDED(SeekSample(m_Track->GetId(), static_cast<AP4_UI64>((pts+ m_presentationTimeOffset)*(double)m_Track->GetMediaTimeScale()), sampleIndex, preceeding)))
    {
      if (m_Decrypter)
        m_Decrypter->SetSampleIndex(sampleIndex);
      m_started = true;
      return AP4_SUCCEEDED(ReadSample());
    }
    return false;
  };
  void SetObserver(FragmentObserver *observer) { m_Observer = observer; };
  void SetPTSOffset(uint64_t offset) { FindTracker(m_Track->GetId())->m_NextDts = offset; };
  uint64_t GetFragmentDuration() { return dynamic_cast<AP4_FragmentSampleTable*>(FindTracker(m_Track->GetId())->m_SampleTable)->GetDuration(); };
  uint32_t GetTimeScale() { return m_Track->GetMediaTimeScale(); };

protected:
  virtual AP4_Result ProcessMoof(AP4_ContainerAtom* moof,
    AP4_Position       moof_offset,
    AP4_Position       mdat_payload_offset)
  {
    AP4_Result result;

    if (m_Observer)
      m_Observer->BeginFragment(m_StreamId);

    if (AP4_SUCCEEDED((result = AP4_LinearReader::ProcessMoof(moof, moof_offset, mdat_payload_offset))))
    {

      //Check if the sample table description has changed
      AP4_ContainerAtom *traf = AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof->GetChild(AP4_ATOM_TYPE_TRAF, 0));
      AP4_TfhdAtom *tfhd = AP4_DYNAMIC_CAST(AP4_TfhdAtom, traf->GetChild(AP4_ATOM_TYPE_TFHD, 0));
      if ((tfhd && tfhd->GetSampleDescriptionIndex() != m_SampleDescIndex) || (!tfhd && (m_SampleDescIndex = 1)))
      {
        m_SampleDescIndex = tfhd->GetSampleDescriptionIndex();
        UpdateSampleDescription();
      }

      if (m_Protected_desc)
      {
        //Setup the decryption
        AP4_CencSampleInfoTable *sample_table;
        AP4_UI32 algorithm_id = 0;

        delete m_Decrypter;
        m_Decrypter = 0;

        AP4_ContainerAtom *traf = AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof->GetChild(AP4_ATOM_TYPE_TRAF, 0));

        if (!m_Protected_desc || !traf)
          return AP4_ERROR_INVALID_FORMAT;

        if (AP4_FAILED(result = AP4_CencSampleInfoTable::Create(m_Protected_desc, traf, algorithm_id, *m_FragmentStream, moof_offset, sample_table)))
          return result;

        AP4_ContainerAtom *schi;
        m_DefaultKey = 0;
        if (m_Protected_desc->GetSchemeInfo() && (schi = m_Protected_desc->GetSchemeInfo()->GetSchiAtom()))
        {
          AP4_TencAtom* tenc(AP4_DYNAMIC_CAST(AP4_TencAtom, schi->GetChild(AP4_ATOM_TYPE_TENC, 0)));
          if (tenc)
            m_DefaultKey = tenc->GetDefaultKid();
        }

        if (AP4_FAILED(result = AP4_CencSampleDecrypter::Create(sample_table, algorithm_id, 0, 0, 0, m_SingleSampleDecryptor, m_Decrypter)))
          return result;
      }
    }

    if (m_Observer)
      m_Observer->EndFragment(m_StreamId);

    return result;
  }

private:

  void UpdateSampleDescription()
  {
    if (m_codecHandler)
      delete m_codecHandler;
    m_codecHandler = 0;
    m_bSampleDescChanged = true;

    AP4_SampleDescription *desc(m_Track->GetSampleDescription(m_SampleDescIndex - 1));
    if (desc->GetType() == AP4_SampleDescription::TYPE_PROTECTED)
    {
      m_Protected_desc = static_cast<AP4_ProtectedSampleDescription*>(desc);
      desc = m_Protected_desc->GetOriginalSampleDescription();
    }
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
      m_codecHandler = new HEVCCodecHandler(desc);
      break;
    case AP4_SAMPLE_FORMAT_MP4A:
      m_codecHandler = new MPEGCodecHandler(desc);
      break;
    default:
      m_codecHandler = new CodecHandler(desc);
      break;
    }
  }

private:
  AP4_Track *m_Track;
  AP4_UI32 m_StreamId;
  AP4_UI32 m_SampleDescIndex;
  bool m_bSampleDescChanged;
  unsigned int m_fail_count_;

  bool m_eos, m_started;
  double m_dts, m_pts;
  double m_presentationTimeOffset;

  AP4_Sample     m_sample_;
  AP4_DataBuffer m_encrypted, m_sample_data_;

  CodecHandler *m_codecHandler;
  const AP4_UI08 *m_DefaultKey;

  AP4_ProtectedSampleDescription *m_Protected_desc;
  AP4_CencSingleSampleDecrypter *m_SingleSampleDecryptor;
  AP4_CencSampleDecrypter *m_Decrypter;
  FragmentObserver *m_Observer;
};

/*******************************************************
Main class Session
********************************************************/
Session *session = 0;

void Session::STREAM::disable()
{
  if (enabled)
  {
    stream_.stop();
    SAFE_DELETE(reader_);
    SAFE_DELETE(input_file_);
    SAFE_DELETE(input_);
    enabled = false;
  }
}

Session::Session(MANIFEST_TYPE manifestType, const char *strURL, const char *strLicType, const char* strLicKey, const char* strLicData, const char* strCert, const std::map<std::string, std::string> &manifestHeaders, const std::map<std::string, std::string> &mediaHeaders, const char* profile_path)
  : manifest_type_(manifestType)
  , mpdFileURL_(strURL)
  , license_key_(strLicKey)
  , license_type_(strLicType)
  , license_data_(strLicData)
  , media_headers_(mediaHeaders)
  , profile_path_(profile_path)
  , decrypterModule_(0)
  , decrypter_(0)
  , adaptiveTree_(0)
  , width_(kodiDisplayWidth)
  , height_(kodiDisplayHeight)
  , changed_(false)
  , manual_streams_(false)
  , last_pts_(0)
  , single_sample_decryptor_(0)
{
  switch (manifest_type_)
  {
  case MANIFEST_TYPE_MPD:
    adaptiveTree_ = new adaptive::DASHTree;
    break;
  case MANIFEST_TYPE_ISM:
    adaptiveTree_ = new adaptive::SmoothTree;
    break;
  default:;
  };

  std::string fn(profile_path_ + "bandwidth.bin");
  FILE* f = fopen(fn.c_str(), "rb");
  if (f)
  {
    double val;
    fread(&val, sizeof(double), 1, f);
    adaptiveTree_->bandwidth_ = static_cast<uint32_t>(val * 8);
    adaptiveTree_->set_download_speed(val);
    fclose(f);
  }
  else
    adaptiveTree_->bandwidth_ = 4000000;
  xbmc->Log(ADDON::LOG_DEBUG, "Initial bandwidth: %u ", adaptiveTree_->bandwidth_);

  int buf;
  xbmc->GetSetting("MAXRESOLUTION", (char*)&buf);
  xbmc->Log(ADDON::LOG_DEBUG, "MAXRESOLUTION selected: %d ", buf);
  switch (buf)
  {
  case 0:
    maxwidth_ = 0xFFFF;
    maxheight_ = 0xFFFF;
    break;
  case 2:
    maxwidth_ = 1920;
    maxheight_ = 1080;
    break;
  default:
    maxwidth_ = 1280;
    maxheight_ = 720;
  }
  if (width_ > maxwidth_)
    width_ = maxwidth_;

  if (height_ > maxheight_)
    height_ = maxheight_;

  xbmc->GetSetting("STREAMSELECTION", (char*)&buf);
  xbmc->Log(ADDON::LOG_DEBUG, "STREAMSELECTION selected: %d ", buf);
  manual_streams_ = buf != 0;

  xbmc->GetSetting("MEDIATYPE", (char*)&buf);
  switch (buf)
  {
  case 1:
    media_type_mask_ = static_cast<uint8_t>(1U) << adaptive::AdaptiveTree::AUDIO;
    break;
  case 2:
    media_type_mask_ = static_cast<uint8_t>(1U) << adaptive::AdaptiveTree::VIDEO;
    break;
  default:
    media_type_mask_ = static_cast<uint8_t>(~0);
  }
  if (*strCert)
  {
    unsigned int sz(strlen(strCert)), dstsz((sz * 3) / 4);
    server_certificate_.SetDataSize(dstsz);
    b64_decode(strCert, sz, server_certificate_.UseData(), dstsz);
    server_certificate_.SetDataSize(dstsz);
  }
  xbmc->Log(ADDON::LOG_DEBUG, "first key and value: %d", manifestHeaders.size());
  adaptiveTree_->manifest_headers_ = manifestHeaders;

}

Session::~Session()
{
  for (std::vector<STREAM*>::iterator b(streams_.begin()), e(streams_.end()); b != e; ++b)
    SAFE_DELETE(*b);
  streams_.clear();

  if (decrypterModule_)
  {
    dlclose(decrypterModule_);
    decrypterModule_ = 0;
    decrypter_ = 0;
  }

  std::string fn(profile_path_ + "bandwidth.bin");
  FILE* f = fopen(fn.c_str(), "wb");
  if (f)
  {
    double val(adaptiveTree_->get_average_download_speed());
    fwrite((const char*)&val, sizeof(double), 1, f);
    fclose(f);
  }
  delete adaptiveTree_;
  adaptiveTree_ = nullptr;
}

void Session::GetSupportedDecrypterURN(std::pair<std::string, std::string> &urn)
{
  typedef SSD_DECRYPTER *(*CreateDecryptorInstanceFunc)(SSD_HOST *host, uint32_t version);

  char specialpath[1024];
  if (!xbmc->GetSetting("DECRYPTERPATH", specialpath))
  {
    xbmc->Log(ADDON::LOG_DEBUG, "DECRYPTERPATH not specified in settings.xml");
    return;
  }
  addonstring path(xbmc->TranslateSpecialProtocol(specialpath));

  kodihost.SetLibraryPath(path.c_str());

  VFSDirEntry *items(0);
  unsigned int num_items(0);

  xbmc->Log(ADDON::LOG_DEBUG, "Searching for decrypters in: %s", path.c_str());

  if (!xbmc->GetDirectory(path.c_str(), "", &items, &num_items))
    return;

  for (unsigned int i(0); i < num_items; ++i)
  {
    if (strncmp(items[i].label, "ssd_", 4) && strncmp(items[i].label, "libssd_", 7))
      continue;

    void * mod(dlopen(items[i].path, RTLD_LAZY));
    if (mod)
    {
      CreateDecryptorInstanceFunc startup;
      if ((startup = (CreateDecryptorInstanceFunc)dlsym(mod, "CreateDecryptorInstance")))
      {
        SSD_DECRYPTER *decrypter = startup(&kodihost, SSD_HOST::version);
        const char *suppUrn(0);

        if (decrypter && (suppUrn = decrypter->Supported(license_type_.c_str(), license_key_.c_str())))
        {
          xbmc->Log(ADDON::LOG_DEBUG, "Found decrypter: %s", items[i].path);
          decrypterModule_ = mod;
          decrypter_ = decrypter;
          urn.first = suppUrn;
          break;
        }
      }
      dlclose(mod);
    }
    else
    {
      xbmc->Log(ADDON::LOG_DEBUG, "%s", dlerror());
    }
  }
  xbmc->FreeDirectory(items, num_items);
}

AP4_CencSingleSampleDecrypter *Session::CreateSingleSampleDecrypter(AP4_DataBuffer &streamCodec)
{
  if (decrypter_)
    return decrypter_->CreateSingleSampleDecrypter(streamCodec, server_certificate_);
  return 0;
};

/*----------------------------------------------------------------------
|   initialize
+---------------------------------------------------------------------*/

bool Session::initialize()
{
  if (!adaptiveTree_)
    return false;

  // Get URN's wich are supported by this addon
  if (!license_type_.empty())
  {
    GetSupportedDecrypterURN(adaptiveTree_->adp_pssh_);
    xbmc->Log(ADDON::LOG_DEBUG, "Supported URN: %s", adaptiveTree_->adp_pssh_.first.c_str());
  }

  // Open mpd file
  size_t paramPos = mpdFileURL_.find('?');
  adaptiveTree_->base_url_ = (paramPos == std::string::npos) ? mpdFileURL_ : mpdFileURL_.substr(0, paramPos);

  paramPos = adaptiveTree_->base_url_.find_last_of('/', adaptiveTree_->base_url_.length());
  if (paramPos == std::string::npos)
  {
    xbmc->Log(ADDON::LOG_ERROR, "Invalid mpdURL: / expected (%s)", mpdFileURL_.c_str());
    return false;
  }
  adaptiveTree_->base_url_.resize(paramPos + 1);

  if (!adaptiveTree_->open(mpdFileURL_.c_str()) || adaptiveTree_->empty())
  {
    xbmc->Log(ADDON::LOG_ERROR, "Could not open / parse mpdURL (%s)", mpdFileURL_.c_str());
    return false;
  }
  xbmc->Log(ADDON::LOG_INFO, "Successfully parsed .mpd file. #Streams: %d Download speed: %0.4f Bytes/s", adaptiveTree_->periods_[0]->adaptationSets_.size(), adaptiveTree_->download_speed_);

  if (adaptiveTree_->encryptionState_ == adaptive::AdaptiveTree::ENCRYTIONSTATE_ENCRYPTED)
  {
    xbmc->Log(ADDON::LOG_ERROR, "Unable to handle decryption. Unsupported!");
    return false;
  }

  uint32_t min_bandwidth(0), max_bandwidth(0);
  {
    int buf;
    xbmc->GetSetting("MINBANDWIDTH", (char*)&buf); min_bandwidth = buf;
    xbmc->GetSetting("MAXBANDWIDTH", (char*)&buf); max_bandwidth = buf;
  }

  // create SESSION::STREAM objects. One for each AdaptationSet
  unsigned int i(0);
  const adaptive::AdaptiveTree::AdaptationSet *adp;

  for (std::vector<STREAM*>::iterator b(streams_.begin()), e(streams_.end()); b != e; ++b)
    SAFE_DELETE(*b);
  streams_.clear();

  while ((adp = adaptiveTree_->GetAdaptationSet(i++)))
  {
    size_t repId = manual_streams_ ? adp->repesentations_.size() : 0;

    do {
      streams_.push_back(new STREAM(*adaptiveTree_, adp->type_));
      STREAM &stream(*streams_.back());
      stream.stream_.prepare_stream(adp, width_, height_, min_bandwidth, max_bandwidth, repId, media_headers_);

      switch (adp->type_)
      {
      case adaptive::AdaptiveTree::VIDEO:
        stream.info_.m_streamType = INPUTSTREAM_INFO::TYPE_VIDEO;
        break;
      case adaptive::AdaptiveTree::AUDIO:
        stream.info_.m_streamType = INPUTSTREAM_INFO::TYPE_AUDIO;
        break;
      case adaptive::AdaptiveTree::TEXT:
        stream.info_.m_streamType = INPUTSTREAM_INFO::TYPE_TELETEXT;
        break;
      default:
        break;
      }
      stream.info_.m_pID = i | (repId << 16);
      strcpy(stream.info_.m_language, adp->language_.c_str());
      stream.info_.m_ExtraData = nullptr;
      stream.info_.m_ExtraSize = 0;
      stream.encrypted = adp->encrypted;

      UpdateStream(stream);

    } while (repId--);
  }

  // Try to initialize an SingleSampleDecryptor
  if (adaptiveTree_->encryptionState_)
  {
    AP4_DataBuffer init_data;

    if (adaptiveTree_->pssh_.second == "FILE")
    {
      if (license_data_.empty())
      {
        std::string strkey(adaptiveTree_->adp_pssh_.first.substr(9));
        size_t pos;
        while ((pos = strkey.find('-')) != std::string::npos)
          strkey.erase(pos, 1);
        if (strkey.size() != 32)
        {
          xbmc->Log(ADDON::LOG_ERROR, "Key system mismatch (%s)!", adaptiveTree_->adp_pssh_.first.c_str());
          return false;
        }

        unsigned char key_system[16];
        AP4_ParseHex(strkey.c_str(), key_system, 16);

        Session::STREAM *stream(streams_[0]);

        stream->enabled = true;
        stream->stream_.start_stream(0, width_, height_);
        stream->stream_.select_stream(true, false, stream->info_.m_pID >> 16);

        stream->input_ = new AP4_DASHStream(&stream->stream_);
        stream->input_file_ = new AP4_File(*stream->input_, AP4_DefaultAtomFactory::Instance, true);
        AP4_Movie* movie = stream->input_file_->GetMovie();
        if (movie == NULL)
        {
          xbmc->Log(ADDON::LOG_ERROR, "No MOOV in stream!");
          stream->disable();
          return false;
        }
        AP4_Array<AP4_PsshAtom*>& pssh = movie->GetPsshAtoms();

        for (unsigned int i = 0; !init_data.GetDataSize() && i < pssh.ItemCount(); i++)
        {
          if (memcmp(pssh[i]->GetSystemId(), key_system, 16) == 0)
            init_data.AppendData(pssh[i]->GetData().GetData(), pssh[i]->GetData().GetDataSize());
        }

        if (!init_data.GetDataSize())
        {
          xbmc->Log(ADDON::LOG_ERROR, "Could not extract license from video stream (PSSH not found)");
          stream->disable();
          return false;
        }
        stream->disable();
      }
      else if (!adaptiveTree_->defaultKID_.empty())
      {
        init_data.SetDataSize(16);
        AP4_Byte *data(init_data.UseData());
        const char *src(adaptiveTree_->defaultKID_.c_str());
        AP4_ParseHex(src, data, 4);
        AP4_ParseHex(src + 9, data + 4, 2);
        AP4_ParseHex(src + 14, data + 6, 2);
        AP4_ParseHex(src + 19, data + 8, 2);
        AP4_ParseHex(src + 24, data + 10, 6);

        uint8_t ld[1024];
        unsigned int ld_size(1014);
        b64_decode(license_data_.c_str(), license_data_.size(), ld, ld_size);

        uint8_t *uuid((uint8_t*)strstr((const char*)ld, "{KID}"));
        if (uuid)
        {
          memmove(uuid + 11, uuid, ld_size - (uuid - ld));
          memcpy(uuid, init_data.GetData(), init_data.GetDataSize());
          init_data.SetData(ld, ld_size + 11);
        }
        else
          init_data.SetData(ld, ld_size);
      }
      else
        return false;
    }
    else
    {
      if (manifest_type_ == MANIFEST_TYPE_ISM)
      {
        create_ism_license(adaptiveTree_->defaultKID_, license_data_, init_data);
      }
      else
      {
        init_data.SetBufferSize(1024);
        unsigned int init_data_size(1024);
        b64_decode(adaptiveTree_->pssh_.second.data(), adaptiveTree_->pssh_.second.size(), init_data.UseData(), init_data_size);
        init_data.SetDataSize(init_data_size);
      }
    }
    if ((single_sample_decryptor_ = CreateSingleSampleDecrypter(init_data)) != 0)
    {
#ifdef ANDROID
      AP4_DataBuffer in;
      m_cryptoData.Reserve(1024);
      single_sample_decryptor_->DecryptSampleData(in, m_cryptoData, 0, 0, 0, 0);
#endif
      return true;
    }
    return false;
  }
  return true;
}

void Session::UpdateStream(STREAM &stream)
{
  const adaptive::AdaptiveTree::Representation *rep(stream.stream_.getRepresentation());

  stream.info_.m_Width = rep->width_;
  stream.info_.m_Height = rep->height_;
  stream.info_.m_Aspect = rep->aspect_;

  if (!stream.info_.m_ExtraSize && rep->codec_private_data_.size())
  {
    stream.info_.m_ExtraSize = rep->codec_private_data_.size();
    stream.info_.m_ExtraData = (const uint8_t*)malloc(stream.info_.m_ExtraSize);
    memcpy((void*)stream.info_.m_ExtraData, rep->codec_private_data_.data(), stream.info_.m_ExtraSize);
  }

  // we currently use only the first track!
  std::string::size_type pos = rep->codecs_.find(",");
  if (pos == std::string::npos)
    pos = rep->codecs_.size();

  strncpy(stream.info_.m_codecInternalName, rep->codecs_.c_str(), pos);
  stream.info_.m_codecInternalName[pos] = 0;

  if (rep->codecs_.find("mp4a") == 0
  || rep->codecs_.find("aac") == 0)
    strcpy(stream.info_.m_codecName, "aac");
  else if (rep->codecs_.find("ec-3") == 0 || rep->codecs_.find("ac-3") == 0)
    strcpy(stream.info_.m_codecName, "eac3");
  else if (rep->codecs_.find("avc") == 0
  || rep->codecs_.find("h264") == 0)
    strcpy(stream.info_.m_codecName, "h264");
  else if (rep->codecs_.find("hevc") == 0 || rep->codecs_.find("hvc") == 0)
    strcpy(stream.info_.m_codecName, "hevc");
  else if (rep->codecs_.find("vp9") == 0)
    strcpy(stream.info_.m_codecName, "vp9");
  else if (rep->codecs_.find("opus") == 0)
    strcpy(stream.info_.m_codecName, "opus");
  else if (rep->codecs_.find("vorbis") == 0)
    strcpy(stream.info_.m_codecName, "vorbis");

  stream.info_.m_FpsRate = rep->fpsRate_;
  stream.info_.m_FpsScale = rep->fpsScale_;
  stream.info_.m_SampleRate = rep->samplingRate_;
  stream.info_.m_Channels = rep->channelCount_;
  stream.info_.m_BitRate = rep->bandwidth_;
}

FragmentedSampleReader *Session::GetNextSample()
{
  STREAM *res(0);
  for (std::vector<STREAM*>::const_iterator b(streams_.begin()), e(streams_.end()); b != e; ++b)
  {
    bool bStarted(false);
    if ((*b)->enabled && !(*b)->reader_->EOS() && AP4_SUCCEEDED((*b)->reader_->Start(bStarted))
      && (!res || (*b)->reader_->DTS() < res->reader_->DTS()))
      res = *b;

    if (bStarted && ((*b)->reader_->GetInformation((*b)->info_)))
      changed_ = true;
  }

  if (res)
  {
    if (res->reader_->GetInformation(res->info_))
      changed_ = true;
    last_pts_ = res->reader_->PTS();
    return res->reader_;
  }
  return 0;
}

bool Session::SeekTime(double seekTime, unsigned int streamId, bool preceeding)
{
  bool ret(false);

  //we don't have pts < 0 here and work internally with uint64
  if (seekTime < 0)
    seekTime = 0;

  for (std::vector<STREAM*>::const_iterator b(streams_.begin()), e(streams_.end()); b != e; ++b)
    if ((*b)->enabled && (streamId == 0 || (*b)->info_.m_pID == streamId))
    {
      bool bReset;
      if ((*b)->stream_.seek_time(seekTime + GetPresentationTimeOffset(), last_pts_, bReset))
      {
        if (bReset)
          (*b)->reader_->Reset(false);
        if (!(*b)->reader_->TimeSeek(seekTime, preceeding))
          (*b)->reader_->Reset(true);
        else
        {
          xbmc->Log(ADDON::LOG_INFO, "seekTime(%0.4f) for Stream:%d continues at %0.4f", seekTime, (*b)->info_.m_pID, (*b)->reader_->PTS());
          ret = true;
        }
      }
      else
        (*b)->reader_->Reset(true);
    }
  return ret;
}

void Session::BeginFragment(AP4_UI32 streamId)
{
  STREAM *s(streams_[streamId - 1]);
  s->reader_->SetPTSOffset(s->stream_.GetPTSOffset());
}

void Session::EndFragment(AP4_UI32 streamId)
{
  STREAM *s(streams_[streamId - 1]);
  adaptiveTree_->SetFragmentDuration(
    s->stream_.getAdaptationSet(),
    s->stream_.getRepresentation(),
    s->stream_.getSegmentPos(),
    s->reader_->GetFragmentDuration(),
    s->reader_->GetTimeScale());
}

const AP4_UI08 *Session::GetDefaultKeyId() const
{
  static const AP4_UI08 default_key[16] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
  if (adaptiveTree_->defaultKID_.size() == 16)
    return reinterpret_cast<const AP4_UI08 *>(adaptiveTree_->defaultKID_.data());
  return default_key;
}

/***************************  Interface *********************************/

#include "kodi_inputstream_dll.h"
#include "libKODI_inputstream.h"

CHelper_libKODI_inputstream *ipsh = 0;

extern "C" {

  ADDON_STATUS curAddonStatus = ADDON_STATUS_UNKNOWN;

  /***********************************************************
  * Standard AddOn related public library functions
  ***********************************************************/

  ADDON_STATUS ADDON_Create(void* hdl, void* props)
  {
    // initialize globals
    session = nullptr;
    kodiDisplayWidth = 1280;
    kodiDisplayHeight = 720;

    if (!hdl)
      return ADDON_STATUS_UNKNOWN;

    xbmc = new ADDON::CHelper_libXBMC_addon;
    if (!xbmc->RegisterMe(hdl))
    {
      SAFE_DELETE(xbmc);
      return ADDON_STATUS_PERMANENT_FAILURE;
    }
    xbmc->Log(ADDON::LOG_DEBUG, "libXBMC_addon successfully loaded");

    ipsh = new CHelper_libKODI_inputstream;
    if (!ipsh->RegisterMe(hdl))
    {
      SAFE_DELETE(xbmc);
      SAFE_DELETE(ipsh);
      return ADDON_STATUS_PERMANENT_FAILURE;
    }

    xbmc->Log(ADDON::LOG_DEBUG, "ADDON_Create()");

    curAddonStatus = ADDON_STATUS_OK;
    return curAddonStatus;
  }

  ADDON_STATUS ADDON_GetStatus()
  {
    return curAddonStatus;
  }

  void ADDON_Destroy()
  {
    SAFE_DELETE(session);
    if (xbmc)
    {
      xbmc->Log(ADDON::LOG_DEBUG, "ADDON_Destroy()");
      SAFE_DELETE(xbmc);
    }
    SAFE_DELETE(ipsh);
  }

  ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
  {
    xbmc->Log(ADDON::LOG_DEBUG, "ADDON_SetSettings()");
    return ADDON_STATUS_OK;
  }

  void ADDON_Stop()
  {
  }

  /***********************************************************
  * InputSteam Client AddOn specific public library functions
  ***********************************************************/

  bool Open(INPUTSTREAM& props)
  {
    xbmc->Log(ADDON::LOG_DEBUG, "Open()");

    const char *lt(""), *lk(""), *ld(""), *lsc("");
    std::map<std::string, std::string> manh, medh;
    std::string mpd_url = props.m_strURL;
    MANIFEST_TYPE manifest(MANIFEST_TYPE_UNKNOWN);
    for (unsigned int i(0); i < props.m_nCountInfoValues; ++i)
    {
      if (strcmp(props.m_ListItemProperties[i].m_strKey, "inputstream.adaptive.license_type") == 0)
      {
        xbmc->Log(ADDON::LOG_DEBUG, "found inputstream.adaptive.license_type: %s", props.m_ListItemProperties[i].m_strValue);
        lt = props.m_ListItemProperties[i].m_strValue;
      }
      else if (strcmp(props.m_ListItemProperties[i].m_strKey, "inputstream.adaptive.license_key") == 0)
      {
        xbmc->Log(ADDON::LOG_DEBUG, "found inputstream.adaptive.license_key: [not shown]");
        lk = props.m_ListItemProperties[i].m_strValue;
      }
      else if (strcmp(props.m_ListItemProperties[i].m_strKey, "inputstream.adaptive.license_data") == 0)
      {
        xbmc->Log(ADDON::LOG_DEBUG, "found inputstream.adaptive.license_data: [not shown]");
        ld = props.m_ListItemProperties[i].m_strValue;
      }
      else if (strcmp(props.m_ListItemProperties[i].m_strKey, "inputstream.adaptive.server_certificate") == 0)
      {
        xbmc->Log(ADDON::LOG_DEBUG, "found inputstream.adaptive.server_certificate: [not shown]");
        lsc = props.m_ListItemProperties[i].m_strValue;
      }
      else if (strcmp(props.m_ListItemProperties[i].m_strKey, "inputstream.adaptive.stream_headers") == 0)
      {
        xbmc->Log(ADDON::LOG_DEBUG, "found inputstream.adaptive.stream_headers: %s", props.m_ListItemProperties[i].m_strValue);
        parseheader(manh, props.m_ListItemProperties[i].m_strValue);
        medh = manh;
        mpd_url = mpd_url.substr(0, mpd_url.find("|"));
      }
      else if (strcmp(props.m_ListItemProperties[i].m_strKey, "inputstream.adaptive.manifest_type") == 0)
      {
        xbmc->Log(ADDON::LOG_DEBUG, "found inputstream.adaptive.manifest_type: %s", props.m_ListItemProperties[i].m_strValue);
        if (strcmp(props.m_ListItemProperties[i].m_strValue, "mpd") == 0)
          manifest = MANIFEST_TYPE_MPD;
        else if (strcmp(props.m_ListItemProperties[i].m_strValue, "ism") == 0)
          manifest = MANIFEST_TYPE_ISM;
      }
    }

    if (manh.empty() && (mpd_url.find("|") != std::string::npos))
      parseheader(manh, mpd_url.substr(mpd_url.find("|") + 1).c_str());

    if (manifest == MANIFEST_TYPE_UNKNOWN)
    {
      xbmc->Log(ADDON::LOG_ERROR, "Invalid / not given inputstream.adaptive.manifest_type");
      return false;
    }

    kodihost.SetProfilePath(props.m_profileFolder);

    session = new Session(manifest, mpd_url.c_str(), lt, lk, ld, lsc, manh, medh, props.m_profileFolder);

    if (!session->initialize())
    {
      SAFE_DELETE(session);
      return false;
    }
    return true;
  }

  void Close(void)
  {
    xbmc->Log(ADDON::LOG_DEBUG, "Close()");
    SAFE_DELETE(session);
  }

  const char* GetPathList(void)
  {
    return "";
  }

  struct INPUTSTREAM_IDS GetStreamIds()
  {
    xbmc->Log(ADDON::LOG_DEBUG, "GetStreamIds()");
    INPUTSTREAM_IDS iids;

    if(session)
    {
        iids.m_streamCount = 0;
        for (unsigned int i(1); i <= session->GetStreamCount(); ++i)
          if(session->GetMediaTypeMask() & static_cast<uint8_t>(1) << session->GetStream(i)->stream_.get_type())
            iids.m_streamIds[iids.m_streamCount++] = i;
    } else
        iids.m_streamCount = 0;
    return iids;
  }

  struct INPUTSTREAM_CAPABILITIES GetCapabilities()
  {
    xbmc->Log(ADDON::LOG_DEBUG, "GetCapabilities()");
    INPUTSTREAM_CAPABILITIES caps;
    caps.m_supportsIDemux = true;
    caps.m_supportsIPosTime = false;
    caps.m_supportsIDisplayTime = true;
    caps.m_supportsSeek = session && !session->IsLive();
    caps.m_supportsPause = caps.m_supportsSeek;
    return caps;
  }

  struct INPUTSTREAM_INFO GetStream(int streamid)
  {
    static struct INPUTSTREAM_INFO dummy_info = {
      INPUTSTREAM_INFO::TYPE_NONE, "", "", 0, 0, 0, "",
      0, 0, 0, 0, 0.0f,
      0, 0, 0, 0, 0 };

    xbmc->Log(ADDON::LOG_DEBUG, "GetStream(%d)", streamid);

    Session::STREAM *stream(session->GetStream(streamid));

    if (stream)
    {
#ifdef ANDROID
      if (stream->encrypted)
      {
        static AP4_DataBuffer tmp;
        tmp.SetData(session->GetCryptoData().GetData(), session->GetCryptoData().GetDataSize());
        tmp.AppendData(stream->info_.m_ExtraData, stream->info_.m_ExtraSize);
        INPUTSTREAM_INFO tmpInfo = stream->info_;
        tmpInfo.m_ExtraData = tmp.GetData();
        tmpInfo.m_ExtraSize = tmp.GetDataSize();
        return tmpInfo;
      }
#endif
      return stream->info_;
    }
    return dummy_info;
  }

  void EnableStream(int streamid, bool enable)
  {
    xbmc->Log(ADDON::LOG_DEBUG, "EnableStream(%d: %s)", streamid, enable?"true":"false");

    if (!session)
      return;

    Session::STREAM *stream(session->GetStream(streamid));

    if (!stream)
      return;

    if (enable)
    {
      if (stream->enabled)
        return;

      stream->enabled = true;

      stream->stream_.start_stream(~0, session->GetWidth(), session->GetHeight());
      const adaptive::AdaptiveTree::Representation *rep(stream->stream_.getRepresentation());
      xbmc->Log(ADDON::LOG_DEBUG, "Selecting stream with conditions: w: %u, h: %u, bw: %u", 
        stream->stream_.getWidth(), stream->stream_.getHeight(), stream->stream_.getBandwidth());

      if (!stream->stream_.select_stream(true, false, stream->info_.m_pID >> 16))
      {
        xbmc->Log(ADDON::LOG_ERROR, "Unable to select stream!");
        return stream->disable();
      }

      if(rep != stream->stream_.getRepresentation())
      {
        session->UpdateStream(*stream);
        session->CheckChange(true);
      }

      stream->input_ = new AP4_DASHStream(&stream->stream_);
      AP4_Movie* movie(0);
      static const AP4_Track::Type TIDC[adaptive::AdaptiveTree::STREAM_TYPE_COUNT] = { 
        AP4_Track::TYPE_UNKNOWN,
        AP4_Track::TYPE_VIDEO,
        AP4_Track::TYPE_AUDIO,
        AP4_Track::TYPE_TEXT };

      if (session->GetManifestType() == MANIFEST_TYPE_ISM && stream->stream_.getRepresentation()->get_initialization() == nullptr)
      {
        //We'll create a Movie out of the things we got from manifest file
        //note: movie will be deleted in destructor of stream->input_file_
        movie = new AP4_Movie();

        AP4_SyntheticSampleTable* sample_table = new AP4_SyntheticSampleTable();
        AP4_SampleDescription *sample_descryption = new AP4_SampleDescription(AP4_SampleDescription::TYPE_UNKNOWN, 0, 0);
        if (stream->stream_.getAdaptationSet()->encrypted)
        {
          AP4_ContainerAtom schi(AP4_ATOM_TYPE_SCHI);
          schi.AddChild(new AP4_TencAtom(AP4_CENC_ALGORITHM_ID_CTR, 8, session->GetDefaultKeyId()));
          sample_descryption = new AP4_ProtectedSampleDescription(0, sample_descryption, 0, AP4_PROTECTION_SCHEME_TYPE_PIFF, 0, "", &schi);
        }
        sample_table->AddSampleDescription(sample_descryption);

        movie->AddTrack(new AP4_Track(TIDC[stream->stream_.get_type()], sample_table, ~0, stream->stream_.getRepresentation()->timescale_, 0, stream->stream_.getRepresentation()->timescale_, 0, "", 0, 0));
        //Create a dumy MOOV Atom to tell Bento4 its a fragmented stream
        AP4_MoovAtom *moov = new AP4_MoovAtom();
        moov->AddChild(new AP4_ContainerAtom(AP4_ATOM_TYPE_MVEX));
        movie->SetMoovAtom(moov);
      }

      stream->input_file_ = new AP4_File(*stream->input_, AP4_DefaultAtomFactory::Instance, true, movie);
      movie = stream->input_file_->GetMovie();

      if (movie == NULL)
      {
        xbmc->Log(ADDON::LOG_ERROR, "No MOOV in stream!");
        return stream->disable();
      }

      AP4_Track *track = movie->GetTrack(TIDC[stream->stream_.get_type()]);
      if (!track)
      {
        xbmc->Log(ADDON::LOG_ERROR, "No suitable track found in stream");
        return stream->disable();
      }

      stream->reader_ = new FragmentedSampleReader(stream->input_, movie, track, streamid, session->GetSingleSampleDecryptor(), session->GetPresentationTimeOffset());
      stream->reader_->SetObserver(dynamic_cast<FragmentObserver*>(session));

      return;
    }
    return stream->disable();
  }

  int ReadStream(unsigned char*, unsigned int)
  {
    return -1;
  }

  int64_t SeekStream(int64_t, int)
  {
    return -1;
  }

  int64_t PositionStream(void)
  {
    return -1;
  }

  int64_t LengthStream(void)
  {
    return -1;
  }

  void DemuxReset(void)
  {
  }

  void DemuxAbort(void)
  {
  }

  void DemuxFlush(void)
  {
  }

  DemuxPacket* __cdecl DemuxRead(void)
  {
    if (!session)
      return NULL;

    FragmentedSampleReader *sr(session->GetNextSample());

    if (session->CheckChange())
    {
      DemuxPacket *p = ipsh->AllocateDemuxPacket(0);
      p->iStreamId = DMX_SPECIALID_STREAMCHANGE;
      xbmc->Log(ADDON::LOG_DEBUG, "DMX_SPECIALID_STREAMCHANGE");
      return p;
    }

    if (sr)
    {
      const AP4_Sample &s(sr->Sample());
      DemuxPacket *p = ipsh->AllocateDemuxPacket(sr->GetSampleDataSize());
      p->dts = sr->DTS() * 1000000;
      p->pts = sr->PTS() * 1000000;
      p->duration = sr->GetDuration() * 1000000;
      p->iStreamId = sr->GetStreamId();
      p->iGroupId = 0;
      p->iSize = sr->GetSampleDataSize();
      memcpy(p->pData, sr->GetSampleData(), p->iSize);

      //xbmc->Log(ADDON::LOG_DEBUG, "DTS: %0.4f, PTS:%0.4f, ID: %u SZ: %d", p->dts, p->pts, p->iStreamId, p->iSize);

      sr->ReadSample();
      return p;
    }
    return NULL;
  }

  bool DemuxSeekTime(double time, bool backwards, double *startpts)
  {
    if (!session)
      return false;

    xbmc->Log(ADDON::LOG_INFO, "DemuxSeekTime (%0.4lf)", time);

    return session->SeekTime(time * 0.001f, 0, !backwards);
  }

  void DemuxSetSpeed(int speed)
  {

  }

  //callback - will be called from kodi
  void SetVideoResolution(int width, int height)
  {
    xbmc->Log(ADDON::LOG_INFO, "SetVideoResolution (%d x %d)", width, height);
    if (session)
      session->SetVideoResolution(width, height);
    else
    {
      kodiDisplayWidth = width;
      kodiDisplayHeight = height;
    }
  }

  int GetTotalTime()
  {
    if (!session)
      return 0;

    return static_cast<int>(session->GetTotalTime()*1000);
  }

  int GetTime()
  {
    if (!session)
      return 0;

    return static_cast<int>(session->GetPTS() * 1000);
  }

  bool CanPauseStream(void)
  {
    return true;
  }

  bool CanSeekStream(void)
  {
    return session && !session->IsLive();
  }

  bool PosTime(int)
  {
    return false;
  }

  void SetSpeed(int)
  {
  }

  void PauseStream(double)
  {
  }

  bool IsRealTimeStream(void)
  {
    return false;
  }

}//extern "C"
