/*
 *  Copyright (C) 2016 liberty-developer (https://github.com/liberty-developer)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "../src/common/AdaptiveDecrypter.h"
#include "../src/utils/Base64Utils.h"
#include "../src/utils/DigestMD5Utils.h"
#include "../src/utils/StringUtils.h"
#include "../src/utils/Utils.h"
#include "Helper.h"
#include "cdm/media/cdm/cdm_adapter.h"
#include "cdm/media/cdm/cdm_type_conversion.h"
#include "jsmn.h"
#include "kodi/tools/StringUtils.h"

#include <algorithm>
#include <list>
#include <optional>
#include <thread>
#include <vector>

#include <bento4/Ap4.h>

#ifndef WIDEVINECDMFILENAME
#error "WIDEVINECDMFILENAME must be set"
#endif

using namespace SSD;
using namespace UTILS;
using namespace kodi::tools;

/*******************************************************
CDM
********************************************************/

/*----------------------------------------------------------------------
|   CdmDecryptedBlock implementation
+---------------------------------------------------------------------*/

class CdmDecryptedBlock : public cdm::DecryptedBlock {
public:
  CdmDecryptedBlock() :buffer_(0), timestamp_(0) {};
  virtual ~CdmDecryptedBlock() {};

  virtual void SetDecryptedBuffer(cdm::Buffer* buffer) override { buffer_ = buffer; };
  virtual cdm::Buffer* DecryptedBuffer() override { return buffer_; };

  virtual void SetTimestamp(int64_t timestamp) override { timestamp_ = timestamp; };
  virtual int64_t Timestamp() const override { return timestamp_; };
private:
  cdm::Buffer *buffer_;
  int64_t timestamp_;
};

/*----------------------------------------------------------------------
|   CdmDecryptedBlock implementation
+---------------------------------------------------------------------*/
class CdmBuffer : public cdm::Buffer {
public:
  CdmBuffer(AP4_DataBuffer *buffer) :buffer_(buffer) {};
  virtual ~CdmBuffer() {};

  virtual void Destroy() override {};

  virtual uint32_t Capacity() const override
  {
    return buffer_->GetBufferSize();
  };
  virtual uint8_t* Data() override
  {
    return (uint8_t*)buffer_->GetData();
  };
  virtual void SetSize(uint32_t size) override
  {
    buffer_->SetDataSize(size);
  };
  virtual uint32_t Size() const override
  {
    return buffer_->GetDataSize();
  };
private:
  AP4_DataBuffer *buffer_;
};

/*----------------------------------------------------------------------
|   CdmVideoDecoder implementation
+---------------------------------------------------------------------*/

class CdmFixedBuffer : public cdm::Buffer {
public:
  CdmFixedBuffer() : data_(nullptr), dataSize_(0), capacity_(0), buffer_(nullptr), instance_(nullptr) {};
  virtual ~CdmFixedBuffer() {};

  virtual void Destroy() override
  {
    GLOBAL::Host->ReleaseBuffer(instance_, buffer_);
    delete this;
  };

  virtual uint32_t Capacity() const override
  {
    return capacity_;
  };

  virtual uint8_t* Data() override
  {
    return data_;
  };

  virtual void SetSize(uint32_t size) override
  {
    dataSize_ = size;
  };

  virtual uint32_t Size() const override
  {
    return dataSize_;
  };

  void initialize(void *instance, uint8_t* data, size_t dataSize, void *buffer)
  {
    instance_ = instance;
    data_ = data;
    dataSize_ = 0;
    capacity_ = dataSize;
    buffer_ = buffer;
  }

  void *Buffer() const
  {
    return buffer_;
  };

private:
  uint8_t *data_;
  size_t dataSize_, capacity_;
  void *buffer_;
  void *instance_;
};

/*----------------------------------------------------------------------
|   WV_CencSingleSampleDecrypter
+---------------------------------------------------------------------*/
class WV_DRM;

class WV_CencSingleSampleDecrypter : public Adaptive_CencSingleSampleDecrypter
{
public:
  // methods
  WV_CencSingleSampleDecrypter(WV_DRM &drm, AP4_DataBuffer &pssh, std::string_view defaultKeyId, bool skipSessionMessage, CryptoMode cryptoMode);
  virtual ~WV_CencSingleSampleDecrypter();

  void GetCapabilities(const uint8_t* key, uint32_t media, SSD_DECRYPTER::SSD_CAPS &caps);
  virtual const char *GetSessionId() override;
  void CloseSessionId();
  AP4_DataBuffer GetChallengeData();

  void SetSession(const char* session, uint32_t session_size, const uint8_t *data, size_t data_size)
  {
    std::lock_guard<std::mutex> lock(renewal_lock_);

    session_ = std::string(session, session_size);
    challenge_.SetData(data, data_size);
    LOG::Log(SSDDEBUG, "Opened widevine session ID: %s", session_.c_str());
  }

  void AddSessionKey(const uint8_t *data, size_t data_size, uint32_t status);
  bool HasKeyId(const uint8_t *keyid);

  virtual AP4_Result SetFragmentInfo(AP4_UI32 pool_id,
                                     const AP4_UI08* key,
                                     const AP4_UI08 nal_length_size,
                                     AP4_DataBuffer& annexb_sps_pps,
                                     AP4_UI32 flags,
                                     CryptoInfo cryptoInfo) override;
  virtual AP4_UI32 AddPool() override;
  virtual void RemovePool(AP4_UI32 poolid) override;


  virtual AP4_Result DecryptSampleData(AP4_UI32 pool_id,
    AP4_DataBuffer& data_in,
    AP4_DataBuffer& data_out,

    // always 16 bytes
    const AP4_UI08* iv,

    // pass 0 for full decryption
    unsigned int    subsample_count,

    // array of <subsample_count> integers. NULL if subsample_count is 0
    const AP4_UI16* bytes_of_cleartext_data,

    // array of <subsample_count> integers. NULL if subsample_count is 0
    const AP4_UI32* bytes_of_encrypted_data) override;

  bool OpenVideoDecoder(const SSD_VIDEOINITDATA *initData);
  SSD_DECODE_RETVAL DecryptAndDecodeVideo(void* hostInstance, SSD_SAMPLE* sample);
  SSD_DECODE_RETVAL VideoFrameDataToPicture(void* hostInstance, SSD_PICTURE *picture);
  void ResetVideo();

  void SetDefaultKeyId(std::string_view keyId) override;
  void AddKeyId(std::string_view keyId) override;

private:
  void CheckLicenseRenewal();
  bool SendSessionMessage();

  WV_DRM &drm_;
  std::string session_;
  AP4_DataBuffer pssh_, challenge_;
  std::string m_defaultKeyId;
  struct WVSKEY
  {
    bool operator == (WVSKEY const &other) const { return keyid == other.keyid; };
    std::string keyid;
    cdm::KeyStatus status;
  };
  std::vector<WVSKEY> keys_;

  AP4_UI16 hdcp_version_;
  int hdcp_limit_;
  int resolution_limit_;

  AP4_DataBuffer decrypt_in_, decrypt_out_;

  struct FINFO
  {
    const AP4_UI08 *key_;
    AP4_UI08 nal_length_size_;
    AP4_UI16 decrypter_flags_;
    AP4_DataBuffer annexb_sps_pps_;
    CryptoInfo m_cryptoInfo;
  };
  std::vector<FINFO> fragment_pool_;
  void LogDecryptError(const cdm::Status status, const AP4_UI08* key);
  void SetCdmSubsamples(std::vector<cdm::SubsampleEntry>& subsamples, bool isCbc);
  void RepackSubsampleData(AP4_DataBuffer& dataIn,
                           AP4_DataBuffer& dataOut,
                           size_t& startPos,
                           size_t& cipherPos,
                           const unsigned int subsamplePos,
                           const AP4_UI16* bytesOfCleartextData,
                           const AP4_UI32* bytesOfEncryptedData);
  void UnpackSubsampleData(AP4_DataBuffer& data_in,
                           size_t& startPos,
                           const unsigned int subsamplePos,
                           const AP4_UI16* bytes_of_cleartext_data,
                           const AP4_UI32* bytes_of_encrypted_data);
  void SetInput(cdm::InputBuffer_2& cdmInputBuffer,
                const AP4_DataBuffer& inputData,
                const unsigned int subsampleCount,
                const uint8_t* iv,
                const FINFO& fragInfo,
                const std::vector<cdm::SubsampleEntry>& subsamples);
  uint32_t promise_id_;
  bool drained_;

  std::list<media::CdmVideoFrame> m_videoFrames;
  std::mutex renewal_lock_;
  CryptoMode m_EncryptionMode;

  std::optional<cdm::VideoDecoderConfig_3> m_currentVideoDecConfig;
};


class WV_DRM : public media::CdmAdapterClient
{
public:
  WV_DRM(const char* licenseURL, const AP4_DataBuffer &serverCert, const uint8_t config);
  virtual ~WV_DRM();

  virtual void OnCDMMessage(const char* session, uint32_t session_size, CDMADPMSG msg, const uint8_t *data, size_t data_size, uint32_t status) override;

  virtual cdm::Buffer *AllocateBuffer(size_t sz) override
  {
    SSD_PICTURE pic;
    pic.decodedDataSize = sz;
    if (GLOBAL::Host->GetBuffer(host_instance_, pic))
    {
      CdmFixedBuffer *buf = new CdmFixedBuffer;
      buf->initialize(host_instance_, pic.decodedData, pic.decodedDataSize, pic.buffer);
      return buf;
    }
    return nullptr;
  };

  void insertssd(WV_CencSingleSampleDecrypter* ssd) { ssds.push_back(ssd); };
  void removessd(WV_CencSingleSampleDecrypter* ssd)
  {
    std::vector<WV_CencSingleSampleDecrypter*>::iterator res(std::find(ssds.begin(), ssds.end(), ssd));
    if (res != ssds.end())
      ssds.erase(res);
  };

  media::CdmAdapter *GetCdmAdapter() { return wv_adapter.get(); };
  const std::string &GetLicenseURL() { return license_url_; };

  cdm::Status DecryptAndDecodeFrame(void* hostInstance, cdm::InputBuffer_2 &cdm_in, media::CdmVideoFrame *frame)
  {
    // DecryptAndDecodeFrame calls CdmAdapter::Allocate which calls Host->GetBuffer
    // that cast hostInstance to CInstanceVideoCodec to get the frame buffer
    // so we have temporary set the host instance
    host_instance_ = hostInstance;
    cdm::Status ret = wv_adapter->DecryptAndDecodeFrame(cdm_in, frame);
    host_instance_ = nullptr;
    return ret;
  }

private:
  std::shared_ptr<media::CdmAdapter> wv_adapter;
  std::string license_url_;
  void *host_instance_;

  std::vector<WV_CencSingleSampleDecrypter*> ssds;
};

WV_DRM::WV_DRM(const char* licenseURL, const AP4_DataBuffer &serverCert, const uint8_t config)
  : license_url_(licenseURL)
  , host_instance_(0)
{
  std::string strLibPath = GLOBAL::Host->GetLibraryPath();
  if (strLibPath.empty())
  {
    LOG::Log(SSDERROR, "No Widevine library path specified in settings");
    return;
  }
  strLibPath += WIDEVINECDMFILENAME;

  std::string strBasePath = GLOBAL::Host->GetProfilePath();
  char cSep = strBasePath.back();
  strBasePath += "widevine";
  strBasePath += cSep;
  GLOBAL::Host->CreateDir(strBasePath.c_str());

  //Build up a CDM path to store decrypter specific stuff. Each domain gets it own path
  const char* bspos(strchr(license_url_.c_str(), ':'));
  if (!bspos || bspos[1] != '/' || bspos[2] != '/' || !(bspos = strchr(bspos + 3, '/')))
  {
    LOG::Log(SSDERROR, "Unable to find protocol inside license URL");
    return;
  }
  if (bspos - license_url_.c_str() > 256)
  {
    LOG::Log(SSDERROR, "Length of license URL domain exeeds max. size of 256");
    return;
  }
  char buffer[1024];
  buffer[(bspos - license_url_.c_str()) * 2] = 0;
  AP4_FormatHex(reinterpret_cast<const uint8_t*>(license_url_.c_str()), bspos - license_url_.c_str(), buffer);

  strBasePath += buffer;
  strBasePath += cSep;
  GLOBAL::Host->CreateDir(strBasePath.c_str());

  wv_adapter = std::shared_ptr<media::CdmAdapter>(new media::CdmAdapter(
    "com.widevine.alpha",
    strLibPath,
    strBasePath,
    media::CdmConfig(false, (config & SSD::SSD_DECRYPTER::CONFIG_PERSISTENTSTORAGE) != 0),
    dynamic_cast<media::CdmAdapterClient*>(this)));
  if (!wv_adapter->valid())
  {
    LOG::Log(SSDERROR, "Unable to load widevine shared library (%s)", strLibPath.c_str());
    wv_adapter = nullptr;
    return;
  }

  if (serverCert.GetDataSize())
    wv_adapter->SetServerCertificate(0, serverCert.GetData(), serverCert.GetDataSize());

  // For backward compatibility: If no | is found in URL, use the most common working config
  if (license_url_.find('|') == std::string::npos)
    license_url_ += "|Content-Type=application%2Foctet-stream|R{SSM}|";

  //wv_adapter->GetStatusForPolicy();
  //wv_adapter->QueryOutputProtectionStatus();
}

WV_DRM::~WV_DRM()
{
  if (wv_adapter)
  {
    wv_adapter->RemoveClient();
    wv_adapter = nullptr;
  }
}

void WV_DRM::OnCDMMessage(const char* session, uint32_t session_size, CDMADPMSG msg, const uint8_t *data, size_t data_size, uint32_t status)
{
  LOG::Log(SSDDEBUG, "CDMMessage: %u arrived!", msg);
  std::vector<WV_CencSingleSampleDecrypter*>::iterator b(ssds.begin()), e(ssds.end());
  for (; b != e; ++b)
    if (!(*b)->GetSessionId() || strncmp((*b)->GetSessionId(), session, session_size) == 0)
      break;

  if (b == ssds.end())
    return;

  if (msg == CDMADPMSG::kSessionMessage)
  {
    (*b)->SetSession(session, session_size, data, data_size);
  }
  else if (msg == CDMADPMSG::kSessionKeysChange)
    (*b)->AddSessionKey(data, data_size, status);
};

/*----------------------------------------------------------------------
|   WV_CencSingleSampleDecrypter::WV_CencSingleSampleDecrypter
+---------------------------------------------------------------------*/

WV_CencSingleSampleDecrypter::WV_CencSingleSampleDecrypter(WV_DRM& drm,
                                                           AP4_DataBuffer& pssh,
                                                           std::string_view defaultKeyId,
                                                           bool skipSessionMessage,
                                                           CryptoMode cryptoMode)
  : drm_(drm),
    pssh_(pssh),
    hdcp_version_(99),
    hdcp_limit_(0),
    resolution_limit_(0),
    promise_id_(1),
    drained_(true),
    m_defaultKeyId{defaultKeyId},
    m_EncryptionMode{cryptoMode}
{
  SetParentIsOwner(false);

  if (pssh.GetDataSize() > 4096)
  {
    LOG::LogF(SSDERROR, "PSSH init data with length %u seems not to be cenc init data",
              pssh.GetDataSize());
    return;
  }

  drm_.insertssd(this);

  if (GLOBAL::Host->IsDebugSaveLicense())
  {
    //! @todo: with ssd_wv refactor the path must be combined with
    //!        UTILS::FILESYS::PathCombine
    std::string debugFilePath = GLOBAL::Host->GetProfilePath();
    debugFilePath += "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.init";

    std::string data{reinterpret_cast<const char*>(pssh.GetData()), pssh.GetDataSize()};
    SSD_UTILS::SaveFile(debugFilePath, data);
  }

  if (memcmp(pssh.GetData() + 4, "pssh", 4) != 0)
  {
    unsigned int buf_size = 32 + pssh.GetDataSize();
    uint8_t buf[4096 + 32];

    // This will request a new session and initializes session_id and message members in cdm_adapter.
    // message will be used to create a license request in the step after CreateSession call.
    // Initialization data is the widevine cdm pssh code in google proto style found in mpd schemeIdUri
    static uint8_t proto[] = { 0x00, 0x00, 0x00, 0x63, 0x70, 0x73, 0x73, 0x68, 0x00, 0x00, 0x00, 0x00, 0xed, 0xef, 0x8b, 0xa9,
      0x79, 0xd6, 0x4a, 0xce, 0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed, 0x00, 0x00, 0x00, 0x00 };

    proto[2] = static_cast<uint8_t>((buf_size >> 8) & 0xFF);
    proto[3] = static_cast<uint8_t>(buf_size & 0xFF);
    proto[30] = static_cast<uint8_t>((pssh.GetDataSize() >> 8) & 0xFF);
    proto[31] = static_cast<uint8_t>(pssh.GetDataSize());

    memcpy(buf, proto, sizeof(proto));
    memcpy(&buf[32], pssh.GetData(), pssh.GetDataSize());
    pssh_.SetData(buf, buf_size);
  }

  drm.GetCdmAdapter()->CreateSessionAndGenerateRequest(promise_id_++, cdm::SessionType::kTemporary, cdm::InitDataType::kCenc,
    reinterpret_cast<const uint8_t *>(pssh_.GetData()), pssh_.GetDataSize());

  int retrycount=0;
  while (session_.empty() && ++retrycount < 100)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

  if (session_.empty())
  {
    LOG::LogF(SSDERROR, "Cannot perform License update, no session available");
    return;
  }

  if (skipSessionMessage)
    return;

  while (challenge_.GetDataSize() > 0 && SendSessionMessage());
}

WV_CencSingleSampleDecrypter::~WV_CencSingleSampleDecrypter()
{
  drm_.removessd(this);
}

void WV_CencSingleSampleDecrypter::GetCapabilities(const uint8_t* key, uint32_t media, SSD_DECRYPTER::SSD_CAPS &caps)
{
  caps = { 0, hdcp_version_, hdcp_limit_ };

  if (session_.empty()) {
    LOG::LogF(SSDDEBUG, "Session empty");
    return;
  }

  caps.flags = SSD_DECRYPTER::SSD_CAPS::SSD_SUPPORTS_DECODING;

  if (keys_.empty()) {
    LOG::LogF(SSDDEBUG, "Keys empty");
    return;
  }

  if (!caps.hdcpLimit)
    caps.hdcpLimit = resolution_limit_;

  //caps.flags |= (SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH | SSD_DECRYPTER::SSD_CAPS::SSD_ANNEXB_REQUIRED);
  //return;

  /*for (auto k : keys_)
    if (!key || memcmp(k.keyid.data(), key, 16) == 0)
    {
      if (k.status != 0)
      {
        if (media == SSD_DECRYPTER::SSD_CAPS::SSD_MEDIA_VIDEO)
          caps.flags |= (SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH | SSD_DECRYPTER::SSD_CAPS::SSD_ANNEXB_REQUIRED);
        else
          caps.flags = SSD_DECRYPTER::SSD_CAPS::SSD_INVALID;
      }
      break;
    }
    */
  if ((caps.flags & SSD_DECRYPTER::SSD_CAPS::SSD_SUPPORTS_DECODING) != 0)
  {
    AP4_UI32 poolid(AddPool());
    fragment_pool_[poolid].key_ = key ? key : reinterpret_cast<const uint8_t*>(keys_.front().keyid.data());
    fragment_pool_[poolid].m_cryptoInfo.m_mode = m_EncryptionMode;

    AP4_DataBuffer in, out;
    AP4_UI32 encb[2] = { 1,1 };
    AP4_UI16 clearb[2] = { 5,5 };
    AP4_Byte vf[12]={0,0,0,1,9,255,0,0,0,1,10,255};
    const AP4_UI08 iv[] = { 1,2,3,4,5,6,7,8,0,0,0,0,0,0,0,0 };
    in.SetBuffer(vf,12);
    in.SetDataSize(12);
    try {
      encb[0] = 12;
      clearb[0] = 0;
      if (DecryptSampleData(poolid, in, out, iv, 1, clearb, encb) != AP4_SUCCESS)
      {
        LOG::LogF(SSDDEBUG, "Single decrypt failed, secure path only");
        if (media == SSD_DECRYPTER::SSD_CAPS::SSD_MEDIA_VIDEO)
          caps.flags |= (SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH | SSD_DECRYPTER::SSD_CAPS::SSD_ANNEXB_REQUIRED);
        else
          caps.flags = SSD_DECRYPTER::SSD_CAPS::SSD_INVALID;
      }
      else
      {
        LOG::LogF(SSDDEBUG, "Single decrypt possible");
        caps.flags |= SSD_DECRYPTER::SSD_CAPS::SSD_SINGLE_DECRYPT;
        caps.hdcpVersion = 99;
        caps.hdcpLimit = resolution_limit_;
      }
    }
    catch (const std::exception& e) {
      LOG::LogF(SSDDEBUG, "Decrypt error, assuming secure path: %s", e.what());
      caps.flags |= (SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH | SSD_DECRYPTER::SSD_CAPS::SSD_ANNEXB_REQUIRED);
    }
    RemovePool(poolid);
  } else {
    LOG::LogF(SSDDEBUG, "Decoding not supported");
  }
}

const char *WV_CencSingleSampleDecrypter::GetSessionId()
{
  return session_.empty()? nullptr : session_.c_str();
}

void WV_CencSingleSampleDecrypter::CloseSessionId()
{
  if (!session_.empty())
  {
    LOG::LogF(SSDDEBUG, "Closing widevine session ID: %s", session_.c_str());
    drm_.GetCdmAdapter()->CloseSession(++promise_id_, session_.data(), session_.size());

    LOG::LogF(SSDDEBUG, "Widevine session ID %s closed", session_.c_str());
    session_.clear();
  }
}

AP4_DataBuffer WV_CencSingleSampleDecrypter::GetChallengeData()
{
  return challenge_;
}

void WV_CencSingleSampleDecrypter::CheckLicenseRenewal()
{
  {
    std::lock_guard<std::mutex> lock(renewal_lock_);
    if (!challenge_.GetDataSize())
      return;
  }
  SendSessionMessage();
}

bool WV_CencSingleSampleDecrypter::SendSessionMessage()
{
  std::vector<std::string> blocks{StringUtils::Split(drm_.GetLicenseURL(), '|')};

  if (blocks.size() != 4)
  {
    LOG::LogF(SSDERROR, "Wrong \"|\" blocks in license URL. Four blocks (req | header | body | "
                        "response) are expected in license URL");
    return false;
  }

  if (GLOBAL::Host->IsDebugSaveLicense())
  {
    //! @todo: with ssd_wv refactor the path must be combined with
    //!        UTILS::FILESYS::PathCombine
    std::string debugFilePath = GLOBAL::Host->GetProfilePath();
    debugFilePath += "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.challenge";

    std::string data{reinterpret_cast<const char*>(challenge_.GetData()), challenge_.GetDataSize()};
    SSD_UTILS::SaveFile(debugFilePath, data);
  }

  //Process placeholder in GET String
  std::string::size_type insPos(blocks[0].find("{SSM}"));
  if (insPos != std::string::npos)
  {
    if (insPos > 0 && blocks[0][insPos - 1] == 'B')
    {
      std::string msgEncoded{BASE64::Encode(challenge_.GetData(), challenge_.GetDataSize())};
      msgEncoded = STRING::URLEncode(msgEncoded);
      blocks[0].replace(insPos - 1, 6, msgEncoded);
    }
    else
    {
      LOG::Log(SSDERROR, "Unsupported License request template (command)");
      return false;
    }
  }

  insPos = blocks[0].find("{HASH}");
  if (insPos != std::string::npos)
  {
    DIGEST::MD5 md5;
    md5.Update(challenge_.GetData(), challenge_.GetDataSize());
    md5.Finalize();
    blocks[0].replace(insPos, 6, md5.HexDigest());
  }

  void* file = GLOBAL::Host->CURLCreate(blocks[0].c_str());

  size_t nbRead;
  std::string response, resLimit, contentType;
  char buf[2048];
  bool serverCertRequest;

  //Set our std headers
  GLOBAL::Host->CURLAddOption(file, SSD_HOST::OPTION_PROTOCOL, "acceptencoding", "gzip, deflate");
  GLOBAL::Host->CURLAddOption(file, SSD_HOST::OPTION_PROTOCOL, "seekable", "0");
  GLOBAL::Host->CURLAddOption(file, SSD_HOST::OPTION_HEADER, "Expect", "");

  //Process headers
  std::vector<std::string> headers{StringUtils::Split(blocks[1], '&')};
  for (std::string& headerStr : headers)
  {
    std::vector<std::string> header{StringUtils::Split(headerStr, '=')};
    if (!header.empty())
    {
      StringUtils::Trim(header[0]);
      std::string value;
      if (header.size() > 1)
      {
        StringUtils::Trim(header[1]);
        value = STRING::URLDecode(header[1]);
      }
      GLOBAL::Host->CURLAddOption(file, SSD_HOST::OPTION_PROTOCOL, header[0].c_str(),
                                  value.c_str());
    }
  }

  //Process body
  if (!blocks[2].empty())
  {
    if (blocks[2][0] == '%')
      blocks[2] = STRING::URLDecode(blocks[2]);

    insPos = blocks[2].find("{SSM}");
    if (insPos != std::string::npos)
    {
      std::string::size_type sidPos(blocks[2].find("{SID}"));
      std::string::size_type kidPos(blocks[2].find("{KID}"));

      char fullDecode = 0;
      if (insPos > 1 && sidPos > 1 && kidPos > 1 && (blocks[2][0] == 'b' || blocks[2][0] == 'B') && blocks[2][1] == '{')
      {
        fullDecode = blocks[2][0];
        blocks[2] = blocks[2].substr(2, blocks[2].size() - 3);
        insPos -= 2;
        if (kidPos != std::string::npos)
          kidPos -= 2;
        if (sidPos != std::string::npos)
          sidPos -= 2;
      }

      size_t size_written(0);

      if (insPos > 0)
      {
        if (blocks[2][insPos - 1] == 'B' || blocks[2][insPos - 1] == 'b')
        {
          std::string msgEncoded{BASE64::Encode(challenge_.GetData(), challenge_.GetDataSize())};
          if (blocks[2][insPos - 1] == 'B') {
            msgEncoded = STRING::URLEncode(msgEncoded);
          }
          blocks[2].replace(insPos - 1, 6, msgEncoded);
          size_written = msgEncoded.size();
        }
        else if (blocks[2][insPos - 1] == 'D')
        {
          std::string msgEncoded{STRING::ToDecimal(challenge_.GetData(), challenge_.GetDataSize())};
          blocks[2].replace(insPos - 1, 6, msgEncoded);
          size_written = msgEncoded.size();
        }
        else
        {
          blocks[2].replace(insPos - 1, 6, reinterpret_cast<const char*>(challenge_.GetData()), challenge_.GetDataSize());
          size_written = challenge_.GetDataSize();
        }
      }
      else
      {
        LOG::Log(SSDERROR, "Unsupported License request template (body / ?{SSM})");
        goto SSMFAIL;
      }

      if (sidPos != std::string::npos && insPos < sidPos)
        sidPos += size_written, sidPos -= 6;

      if (kidPos != std::string::npos && insPos < kidPos)
        kidPos += size_written, kidPos -= 6;

      size_written = 0;

      if (sidPos != std::string::npos)
      {
        if (sidPos > 0)
        {
          if (blocks[2][sidPos - 1] == 'B' || blocks[2][sidPos - 1] == 'b')
          {
            std::string msgEncoded{BASE64::Encode(session_)};

            if (blocks[2][sidPos - 1] == 'B') {
              msgEncoded = STRING::URLEncode(msgEncoded);
            }
            
            blocks[2].replace(sidPos - 1, 6, msgEncoded);
            size_written = msgEncoded.size();
          }
          else
          {
            blocks[2].replace(sidPos - 1, 6, session_.data(), session_.size());
            size_written = session_.size();
          }
        }
        else
        {
          LOG::LogF(SSDERROR, "Unsupported License request template (body / ?{SID})");
          goto SSMFAIL;
        }
      }

      if (kidPos != std::string::npos)
      {
        if (sidPos < kidPos)
          kidPos += size_written, kidPos -= 6;

        if (blocks[2][kidPos - 1] == 'H')
        {
          std::string keyIdUUID{StringUtils::ToHexadecimal(m_defaultKeyId)};
          blocks[2].replace(kidPos - 1, 6, keyIdUUID.c_str(), 32);
        }
        else
        {
          std::string kidUUID{ConvertKIDtoUUID(m_defaultKeyId)};
          blocks[2].replace(kidPos, 5, kidUUID.c_str(), 36);
        }
      }

      if (fullDecode)
      {
        std::string msgEncoded{BASE64::Encode(blocks[2])};
        if (fullDecode == 'B')
        {
          msgEncoded = STRING::URLEncode(msgEncoded);
        }
        blocks[2] = msgEncoded;
      }
    }

    std::string encData{BASE64::Encode(blocks[2])};
    GLOBAL::Host->CURLAddOption(file, SSD_HOST::OPTION_PROTOCOL, "postdata", encData.c_str());
  }

  serverCertRequest = challenge_.GetDataSize() == 2;
  challenge_.SetDataSize(0);

  if (!GLOBAL::Host->CURLOpen(file))
  {
    LOG::Log(SSDERROR, "License server returned failure");
    goto SSMFAIL;
  }

  // read the file
  while ((nbRead = GLOBAL::Host->ReadFile(file, buf, 1024)) > 0)
    response += std::string((const char*)buf, nbRead);

  resLimit =
      GLOBAL::Host->CURLGetProperty(file, SSD_HOST::CURLPROPERTY::PROPERTY_HEADER, "X-Limit-Video");
  contentType =
      GLOBAL::Host->CURLGetProperty(file, SSD_HOST::CURLPROPERTY::PROPERTY_HEADER, "Content-Type");

  if (!resLimit.empty())
  {
    std::string::size_type posMax = resLimit.find("max=");
    if (posMax != std::string::npos)
      resolution_limit_ = std::atoi(resLimit.data() + (posMax + 4));
  }

  GLOBAL::Host->CloseFile(file);
  file = 0;

  if (nbRead != 0)
  {
    LOG::LogF(SSDERROR, "Could not read full SessionMessage response");
    goto SSMFAIL;
  }

  if (GLOBAL::Host->IsDebugSaveLicense())
  {
    //! @todo: with ssd_wv refactor the path must be combined with
    //!        UTILS::FILESYS::PathCombine
    std::string debugFilePath = GLOBAL::Host->GetProfilePath();
    debugFilePath += "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.response";

    SSD_UTILS::SaveFile(debugFilePath, response);
  }

  if (serverCertRequest && contentType.find("application/octet-stream") == std::string::npos)
    serverCertRequest = false;

  if (!blocks[3].empty() && blocks[3][0] != 'R' && !serverCertRequest)
  {
    if (blocks[3][0] == 'J' || (blocks[3].size() > 1 && blocks[3][0] == 'B' && blocks[3][1] == 'J'))
    {
      int dataPos = 2;

      if (response.size() >= 3 && blocks[3][0] == 'B')
      {
        response = BASE64::Decode(response);
        dataPos = 3;
      }

      jsmn_parser jsn;
      jsmntok_t tokens[256];

      jsmn_init(&jsn);
      int i(0), numTokens = jsmn_parse(&jsn, response.c_str(), response.size(), tokens, 256);

      std::vector<std::string> jsonVals{StringUtils::Split(blocks[3].substr(dataPos), ';')};

      // Find HDCP limit
      if (jsonVals.size() > 1)
      {
        for (; i < numTokens; ++i)
          if (tokens[i].type == JSMN_STRING && tokens[i].size == 1 && jsonVals[1].size() == static_cast<unsigned int>(tokens[i].end - tokens[i].start)
            && strncmp(response.c_str() + tokens[i].start, jsonVals[1].c_str(), tokens[i].end - tokens[i].start) == 0)
            break;
        if (i < numTokens)
          hdcp_limit_ = std::atoi((response.c_str() + tokens[i + 1].start));
      }
      // Find license key
      if (jsonVals.size() > 0)
      {
        for (i = 0; i < numTokens; ++i)
          if (tokens[i].type == JSMN_STRING && tokens[i].size == 1 && jsonVals[0].size() == static_cast<unsigned int>(tokens[i].end - tokens[i].start)
            && strncmp(response.c_str() + tokens[i].start, jsonVals[0].c_str(), tokens[i].end - tokens[i].start) == 0)
          {
            if (i + 1 < numTokens && tokens[i + 1].type == JSMN_ARRAY && tokens[i + 1].size == 1)
              ++i;
            break;
          }
      }
      else
        i = numTokens;

      if (i < numTokens)
      {
        std::string respData{
            response.substr(tokens[i + 1].start, tokens[i + 1].end - tokens[i + 1].start)};

        if (blocks[3][dataPos - 1] == 'B')
        {
          respData = BASE64::Decode(respData);
        }

        drm_.GetCdmAdapter()->UpdateSession(++promise_id_, session_.data(), session_.size(),
                                            reinterpret_cast<const uint8_t*>(respData.c_str()),
                                            respData.size());
      }
      else
      {
        LOG::LogF(SSDERROR, "Unable to find %s in JSON string", blocks[3].c_str() + 2);
        goto SSMFAIL;
      }
    }
    else if (blocks[3][0] == 'H' && blocks[3].size() >= 2)
    {
      //Find the payload
      std::string::size_type payloadPos = response.find("\r\n\r\n");
      if (payloadPos != std::string::npos)
      {
        payloadPos += 4;
        if (blocks[3][1] == 'B')
          drm_.GetCdmAdapter()->UpdateSession(++promise_id_, session_.data(), session_.size(),
            reinterpret_cast<const uint8_t*>(response.c_str() + payloadPos), response.size() - payloadPos);
        else
        {
          LOG::LogF(SSDERROR, "Unsupported HTTP payload data type definition");
          goto SSMFAIL;
        }
      }
      else
      {
        LOG::LogF(SSDERROR, "Unable to find HTTP payload in response");
        goto SSMFAIL;
      }
    }
    else if (blocks[3][0] == 'B' && blocks[3].size() == 1)
    {
      std::string decRespData{BASE64::Decode(response)};

      drm_.GetCdmAdapter()->UpdateSession(++promise_id_, session_.data(), session_.size(),
                                          reinterpret_cast<const uint8_t*>(decRespData.c_str()),
                                          decRespData.size());
    }
    else
    {
      LOG::LogF(SSDERROR, "Unsupported License request template (response)");
      goto SSMFAIL;
    }
  }
  else // its binary - simply push the returned data as update
  {
    drm_.GetCdmAdapter()->UpdateSession(++promise_id_, session_.data(), session_.size(),
                                        reinterpret_cast<const uint8_t*>(response.data()),
                                        response.size());
  }

  if (keys_.empty())
  {
    LOG::LogF(SSDERROR, "License update not successful (no keys)");
    CloseSessionId();
    return false;
  }

  LOG::Log(SSDDEBUG, "License update successful");
  return true;

SSMFAIL:
  if (file)
    GLOBAL::Host->CloseFile(file);

  return false;
}

void WV_CencSingleSampleDecrypter::AddSessionKey(const uint8_t *data, size_t data_size, uint32_t status)
{
  WVSKEY key;
  std::vector<WVSKEY>::iterator res;

  key.keyid = std::string((const char*)data, data_size);
  if ((res = std::find(keys_.begin(), keys_.end(), key)) == keys_.end())
    res = keys_.insert(res, key);
  res->status = static_cast<cdm::KeyStatus>(status);
}

/*----------------------------------------------------------------------
|   WV_CencSingleSampleDecrypter::SetKeyId
+---------------------------------------------------------------------*/

bool WV_CencSingleSampleDecrypter::HasKeyId(const uint8_t *keyid)
{
  if (keyid)
    for (std::vector<WVSKEY>::const_iterator kb(keys_.begin()), ke(keys_.end()); kb != ke; ++kb)
      if (kb->keyid.size() == 16 && memcmp(kb->keyid.c_str(), keyid, 16) == 0)
        return true;
  return false;
}

AP4_Result WV_CencSingleSampleDecrypter::SetFragmentInfo(AP4_UI32 pool_id,
                                                         const AP4_UI08* key,
                                                         const AP4_UI08 nal_length_size,
                                                         AP4_DataBuffer& annexb_sps_pps,
                                                         AP4_UI32 flags,
                                                         CryptoInfo cryptoInfo)
{
  if (pool_id >= fragment_pool_.size())
    return AP4_ERROR_OUT_OF_RANGE;

  fragment_pool_[pool_id].key_ = key;
  fragment_pool_[pool_id].nal_length_size_ = nal_length_size;
  fragment_pool_[pool_id].annexb_sps_pps_.SetData(annexb_sps_pps.GetData(), annexb_sps_pps.GetDataSize());
  fragment_pool_[pool_id].decrypter_flags_ = flags;
  fragment_pool_[pool_id].m_cryptoInfo = cryptoInfo;

  return AP4_SUCCESS;
}

AP4_UI32 WV_CencSingleSampleDecrypter::AddPool()
{
  for (size_t i(0); i < fragment_pool_.size(); ++i)
    if (fragment_pool_[i].nal_length_size_ == 99)
    {
      fragment_pool_[i].nal_length_size_ = 0;
      return i;
    }
  fragment_pool_.push_back(FINFO());
  fragment_pool_.back().nal_length_size_ = 0;
  return static_cast<AP4_UI32>(fragment_pool_.size() - 1);
}


void WV_CencSingleSampleDecrypter::RemovePool(AP4_UI32 poolid)
{
  fragment_pool_[poolid].nal_length_size_ = 99;
  fragment_pool_[poolid].key_ = nullptr;
}

void WV_CencSingleSampleDecrypter::LogDecryptError(const cdm::Status status, const AP4_UI08* key)
{
  char buf[36];
  buf[32] = 0;
  AP4_FormatHex(key, 16, buf);
  LOG::LogF(SSDDEBUG, "Decrypt failed with error: %d and key: %s", status, buf);
}

void WV_CencSingleSampleDecrypter::SetCdmSubsamples(std::vector<cdm::SubsampleEntry>& subsamples,
                                                    bool isCbc)
{
  if (isCbc)
  {
    subsamples.resize(1);
    subsamples[0] = {0, decrypt_in_.GetDataSize()};
  }
  else
  {
    subsamples.push_back({0, decrypt_in_.GetDataSize()});
  }
}

void WV_CencSingleSampleDecrypter::RepackSubsampleData(AP4_DataBuffer& dataIn,
                                                       AP4_DataBuffer& dataOut,
                                                       size_t& pos,
                                                       size_t& cipherPos,
                                                       const unsigned int subsamplePos,
                                                       const AP4_UI16* bytesOfCleartextData,
                                                       const AP4_UI32* bytesOfEncryptedData)
{
  dataOut.AppendData(dataIn.GetData() + pos, bytesOfCleartextData[subsamplePos]);
  pos += bytesOfCleartextData[subsamplePos];
  dataOut.AppendData(decrypt_out_.GetData() + cipherPos, bytesOfEncryptedData[subsamplePos]);
  pos += bytesOfEncryptedData[subsamplePos];
  cipherPos += bytesOfEncryptedData[subsamplePos];
}

void WV_CencSingleSampleDecrypter::UnpackSubsampleData(AP4_DataBuffer& dataIn,
                                                       size_t& pos,
                                                       const unsigned int subsamplePos,
                                                       const AP4_UI16* bytesOfCleartextData,
                                                       const AP4_UI32* bytesOfEncryptedData)
{
  pos += bytesOfCleartextData[subsamplePos];
  decrypt_in_.AppendData(dataIn.GetData() + pos, bytesOfEncryptedData[subsamplePos]);
  pos += bytesOfEncryptedData[subsamplePos];
}

void WV_CencSingleSampleDecrypter::SetInput(cdm::InputBuffer_2& cdmInputBuffer,
                                            const AP4_DataBuffer& inputData,
                                            const unsigned int subsampleCount,
                                            const uint8_t* iv,
                                            const FINFO& fragInfo,
                                            const std::vector<cdm::SubsampleEntry>& subsamples)
{
  cdmInputBuffer.data = inputData.GetData();
  cdmInputBuffer.data_size = inputData.GetDataSize();
  cdmInputBuffer.num_subsamples = subsampleCount;
  cdmInputBuffer.iv = iv;
  cdmInputBuffer.iv_size = 16; //Always 16, see AP4_CencSingleSampleDecrypter declaration.
  cdmInputBuffer.key_id = fragInfo.key_;
  cdmInputBuffer.key_id_size = 16;
  cdmInputBuffer.subsamples = subsamples.data();
  cdmInputBuffer.encryption_scheme = media::ToCdmEncryptionScheme(fragInfo.m_cryptoInfo.m_mode);
  cdmInputBuffer.timestamp = 0;
  cdmInputBuffer.pattern = {fragInfo.m_cryptoInfo.m_cryptBlocks, fragInfo.m_cryptoInfo.m_skipBlocks};
}

/*----------------------------------------------------------------------
|   WV_CencSingleSampleDecrypter::DecryptSampleData
+---------------------------------------------------------------------*/
AP4_Result WV_CencSingleSampleDecrypter::DecryptSampleData(AP4_UI32 pool_id,
  AP4_DataBuffer& data_in,
  AP4_DataBuffer& data_out,
  const AP4_UI08* iv,
  unsigned int    subsample_count,
  const AP4_UI16* bytes_of_cleartext_data,
  const AP4_UI32* bytes_of_encrypted_data)
{
  if (!drm_.GetCdmAdapter())
  {
    data_out.SetData(data_in.GetData(), data_in.GetDataSize());
    return AP4_SUCCESS;
  }

  FINFO &fragInfo(fragment_pool_[pool_id]);

  if(fragInfo.decrypter_flags_ & SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH) //we can not decrypt only
  {
    if (fragInfo.nal_length_size_ > 4)
    {
      LOG::LogF(SSDERROR, "Nalu length size > 4 not supported");
      return AP4_ERROR_NOT_SUPPORTED;
    }

    AP4_UI16 dummyClear(0);
    AP4_UI32 dummyCipher(data_in.GetDataSize());

    if (iv)
    {
      if (!subsample_count)
      {
        subsample_count = 1;
        bytes_of_cleartext_data = &dummyClear;
        bytes_of_encrypted_data = &dummyCipher;
      }

      data_out.SetData(reinterpret_cast<const AP4_Byte*>(&subsample_count), sizeof(subsample_count));
      data_out.AppendData(reinterpret_cast<const AP4_Byte*>(bytes_of_cleartext_data), subsample_count * sizeof(AP4_UI16));
      data_out.AppendData(reinterpret_cast<const AP4_Byte*>(bytes_of_encrypted_data), subsample_count * sizeof(AP4_UI32));
      data_out.AppendData(reinterpret_cast<const AP4_Byte*>(iv), 16);
      data_out.AppendData(reinterpret_cast<const AP4_Byte*>(fragInfo.key_), 16);
    }
    else
    {
      data_out.SetDataSize(0);
      bytes_of_cleartext_data = &dummyClear;
      bytes_of_encrypted_data = &dummyCipher;
    }

    if (fragInfo.nal_length_size_ && (!iv || bytes_of_cleartext_data[0] > 0))
    {
      //check NAL / subsample
      const AP4_Byte *packet_in(data_in.GetData()), *packet_in_e(data_in.GetData() + data_in.GetDataSize());
      AP4_UI16 *clrb_out(iv ? reinterpret_cast<AP4_UI16*>(data_out.UseData() + sizeof(subsample_count)):nullptr);
      unsigned int nalunitcount(0), nalunitsum(0), configSize(0);

      while (packet_in < packet_in_e)
      {
        uint32_t nalsize(0);
        for (unsigned int i(0); i < fragInfo.nal_length_size_; ++i) { nalsize = (nalsize << 8) + *packet_in++; };

        //look if we have to inject sps / pps
        if (fragInfo.annexb_sps_pps_.GetDataSize() && (*packet_in & 0x1F) != 9 /*AVC_NAL_AUD*/)
        {
          data_out.AppendData(fragInfo.annexb_sps_pps_.GetData(),
                              fragInfo.annexb_sps_pps_.GetDataSize());
          if(clrb_out) *clrb_out += fragInfo.annexb_sps_pps_.GetDataSize();
          configSize = fragInfo.annexb_sps_pps_.GetDataSize();
          fragInfo.annexb_sps_pps_.SetDataSize(0);
        }

        // Annex-B Start pos
        static AP4_Byte annexbStartCode[4] = {0x00, 0x00, 0x00, 0x01};
        data_out.AppendData(annexbStartCode, 4);
        data_out.AppendData(packet_in, nalsize);
        packet_in += nalsize;
        if (clrb_out) *clrb_out += (4 - fragInfo.nal_length_size_);
        ++nalunitcount;

        if (!iv)
        {
          nalunitsum = 0;
        }
        else if (nalsize + fragInfo.nal_length_size_ + nalunitsum >= *bytes_of_cleartext_data + *bytes_of_encrypted_data)
        {
          AP4_UI32 summedBytes(0);
          do
          {
            summedBytes += *bytes_of_cleartext_data + *bytes_of_encrypted_data;
            ++bytes_of_cleartext_data;
            ++bytes_of_encrypted_data;
            ++clrb_out;
            --subsample_count;
          } while (subsample_count && nalsize + fragInfo.nal_length_size_ + nalunitsum > summedBytes);

          if (nalsize + fragInfo.nal_length_size_ + nalunitsum > summedBytes)
          {
            LOG::LogF(SSDERROR, "NAL Unit exceeds subsample definition (nls: %u) %u -> %u ",
                      static_cast<unsigned int>(fragInfo.nal_length_size_),
                      static_cast<unsigned int>(nalsize + fragInfo.nal_length_size_ + nalunitsum),
                      summedBytes);
            return AP4_ERROR_NOT_SUPPORTED;
          }
          nalunitsum = 0;
        }
        else
          nalunitsum += nalsize + fragInfo.nal_length_size_;
      }
      if (packet_in != packet_in_e || subsample_count)
      {
        LOG::Log(SSDERROR, "NAL Unit definition incomplete (nls: %u) %u -> %u ",
                 static_cast<unsigned int>(fragInfo.nal_length_size_),
                 static_cast<unsigned int>(packet_in_e - packet_in), subsample_count);
        return AP4_ERROR_NOT_SUPPORTED;
      }
    }
    else
      data_out.AppendData(data_in.GetData(), data_in.GetDataSize());
    return AP4_SUCCESS;
  }

  if (!fragInfo.key_)
  {
    LOG::LogF(SSDDEBUG, "No Key");
    return AP4_ERROR_INVALID_PARAMETERS;
  }

  data_out.SetDataSize(0);

  uint16_t clearb(0);
  uint32_t cipherb(data_in.GetDataSize());

  // check input parameters
  if (iv == NULL) return AP4_ERROR_INVALID_PARAMETERS;
  if (subsample_count) {
    if (bytes_of_cleartext_data == NULL || bytes_of_encrypted_data == NULL)
    {
      LOG::LogF(SSDDEBUG, "Invalid input params");
      return AP4_ERROR_INVALID_PARAMETERS;
    }
  }
  else
  {
    subsample_count = 1;
    bytes_of_cleartext_data = &clearb;
    bytes_of_encrypted_data = &cipherb;
  }
  cdm::Status ret{cdm::Status::kSuccess};
  std::vector<cdm::SubsampleEntry> subsamples;
  subsamples.reserve(subsample_count);

  bool useCbcDecrypt{fragInfo.m_cryptoInfo.m_mode == CryptoMode::AES_CBC};
  
  // We can only decrypt with subsamples set to 1
  // This must be handled differently for CENC and CBCS
  // CENC:
  // CDM should get 1 block of encrypted data per sample, encrypted data
  // from all subsamples should be formed into a contiguous block.
  // Even if there is only 1 subsample, we should remove cleartext data
  // from it before passing to CDM.
  // CBCS:
  // Due to the nature of this cipher subsamples must be decrypted separately

  const unsigned int iterations{useCbcDecrypt ? subsample_count : 1};
  size_t absPos{0};

  for (unsigned int i{0}; i < iterations; ++i)
  {
    decrypt_in_.Reserve(data_in.GetDataSize());
    decrypt_in_.SetDataSize(0);
    size_t decryptInPos = absPos;
    if (useCbcDecrypt)
    {
      UnpackSubsampleData(data_in, decryptInPos, i, bytes_of_cleartext_data,
                          bytes_of_encrypted_data);
    }
    else
    {
      for (unsigned int subsamplePos{0}; subsamplePos < subsample_count; ++subsamplePos)
      {
        UnpackSubsampleData(data_in, absPos, subsamplePos, bytes_of_cleartext_data, bytes_of_encrypted_data);
      }
    }

    if (decrypt_in_.GetDataSize() > 0) // remember to include when calling setcdmsubsamples
    {
      SetCdmSubsamples(subsamples, useCbcDecrypt);
    }

    else // we have nothing to decrypt in this iteration
    {
      if (useCbcDecrypt)
      {
        data_out.AppendData(data_in.GetData() + absPos, bytes_of_cleartext_data[i]);
        absPos += bytes_of_cleartext_data[i];
        continue;
      }
      else // we can exit here for CENC and just return the input buffer
      {
        data_out.AppendData(data_in.GetData(), data_in.GetDataSize());
        return AP4_SUCCESS;
      }
    }

    cdm::InputBuffer_2 cdm_in;
    SetInput(cdm_in, decrypt_in_, 1, iv, fragInfo, subsamples);
    decrypt_out_.SetDataSize(decrypt_in_.GetDataSize());
    CdmBuffer buf{&decrypt_out_};
    CdmDecryptedBlock cdm_out;
    cdm_out.SetDecryptedBuffer(&buf);

    CheckLicenseRenewal();
    ret = drm_.GetCdmAdapter()->Decrypt(cdm_in, &cdm_out);

    if (ret == cdm::Status::kSuccess)
    {
      size_t cipherPos = 0;
      if (useCbcDecrypt)
      {
        RepackSubsampleData(data_in, data_out, absPos, cipherPos, i, bytes_of_cleartext_data,
                            bytes_of_encrypted_data);
      }
      else
      {
        size_t absPos{0};
        for (unsigned int i{0}; i < subsample_count; ++i)
        {
          RepackSubsampleData(data_in, data_out, absPos, cipherPos, i, bytes_of_cleartext_data,
                              bytes_of_encrypted_data);
        }
      }
    }
    else
    {
      LogDecryptError(ret, fragInfo.key_);
    }
  }
  return (ret == cdm::Status::kSuccess) ? AP4_SUCCESS : AP4_ERROR_INVALID_PARAMETERS;
}

bool WV_CencSingleSampleDecrypter::OpenVideoDecoder(const SSD_VIDEOINITDATA* initData)
{
  cdm::VideoDecoderConfig_3 vconfig = media::ToCdmVideoDecoderConfig(initData, m_EncryptionMode);

  // InputStream interface call OpenVideoDecoder also during playback when stream quality
  // change, so we reinitialize the decoder only when the codec change
  if (m_currentVideoDecConfig.has_value())
  {
    cdm::VideoDecoderConfig_3& currVidConfig = *m_currentVideoDecConfig;
    if (currVidConfig.codec == vconfig.codec && currVidConfig.profile == vconfig.profile)
      return true;

    drm_.GetCdmAdapter()->DeinitializeDecoder(cdm::StreamType::kStreamTypeVideo);
  }

  m_currentVideoDecConfig = vconfig;

  cdm::Status ret = drm_.GetCdmAdapter()->InitializeVideoDecoder(vconfig);
  m_videoFrames.clear();
  drained_ = true;

  LOG::LogF(SSDDEBUG, "Initialization returned status: %s",
            media::CdmStatusToString(ret).c_str());
  return ret == cdm::Status::kSuccess;
}

SSD_DECODE_RETVAL WV_CencSingleSampleDecrypter::DecryptAndDecodeVideo(void* hostInstance, SSD_SAMPLE* sample)
{
  // if we have an picture waiting, or not yet get the dest buffer, do nothing
  if (m_videoFrames.size() == 4)
    return VC_ERROR;

  if (sample->cryptoInfo.numSubSamples > 0 &&
      (!sample->cryptoInfo.clearBytes || !sample->cryptoInfo.cipherBytes))
  {
    return VC_ERROR;
  }

  cdm::InputBuffer_2 inputBuffer{};
  std::vector<cdm::SubsampleEntry> subsamples;

  media::ToCdmInputBuffer(sample, &subsamples, &inputBuffer);

  if (sample->dataSize > 0)
    drained_ = false;

  //LICENSERENEWAL:
  CheckLicenseRenewal();

  media::CdmVideoFrame videoFrame;
  cdm::Status status = drm_.DecryptAndDecodeFrame(hostInstance, inputBuffer, &videoFrame);

  if (status == cdm::Status::kSuccess)
  {
    std::list<media::CdmVideoFrame>::iterator f(m_videoFrames.begin());
    while (f != m_videoFrames.end() && f->Timestamp() < videoFrame.Timestamp())
    {
      ++f;
    }
    m_videoFrames.insert(f, videoFrame);
    return VC_NONE;
  }
  else if (status == cdm::Status::kNeedMoreData && inputBuffer.data)
  {
    return VC_NONE;
  }
  else if (status == cdm::Status::kNoKey)
  {
    char buf[36];
    buf[0] = 0;
    buf[32] = 0;
    AP4_FormatHex(inputBuffer.key_id, inputBuffer.key_id_size, buf);
    LOG::LogF(SSDERROR, "Returned CDM status kNoKey for key %s", buf);
    return VC_EOF;
  }

  LOG::LogF(SSDDEBUG, "Returned CDM status: %i", status);
  return VC_ERROR;
}

SSD_DECODE_RETVAL WV_CencSingleSampleDecrypter::VideoFrameDataToPicture(void* hostInstance, SSD_PICTURE* picture)
{
  if (m_videoFrames.size() == 4 || (m_videoFrames.size() > 0 && (picture->flags & SSD_PICTURE::FLAG_DRAIN)))
  {
    media::CdmVideoFrame& videoFrame(m_videoFrames.front());

    picture->width = videoFrame.Size().width;
    picture->height = videoFrame.Size().height;
    picture->pts = videoFrame.Timestamp();
    picture->decodedData = videoFrame.FrameBuffer()->Data();
    picture->decodedDataSize = videoFrame.FrameBuffer()->Size();
    picture->buffer = static_cast<CdmFixedBuffer*>(videoFrame.FrameBuffer())->Buffer();

    for (unsigned int i(0); i < cdm::VideoPlane::kMaxPlanes; ++i)
    {
      picture->planeOffsets[i] = videoFrame.PlaneOffset(static_cast<cdm::VideoPlane>(i));
      picture->stride[i] = videoFrame.Stride(static_cast<cdm::VideoPlane>(i));
    }
    picture->videoFormat = media::ToSSDVideoFormat(videoFrame.Format());
    videoFrame.SetFrameBuffer(nullptr); //marker for "No Picture"

    delete (CdmFixedBuffer*)(videoFrame.FrameBuffer());
    m_videoFrames.pop_front();

    return VC_PICTURE;
  }
  else if ((picture->flags & SSD_PICTURE::FLAG_DRAIN))
  {
    static SSD_SAMPLE drainSample{};
    if (drained_ || DecryptAndDecodeVideo(hostInstance, &drainSample) == VC_ERROR)
    {
      drained_ = true;
      return VC_EOF;
    }
    else
      return VC_NONE;
  }

  return VC_BUFFER;
}

void WV_CencSingleSampleDecrypter::ResetVideo()
{
  drm_.GetCdmAdapter()->ResetDecoder(cdm::kStreamTypeVideo);
  drained_ = true;
}

void WV_CencSingleSampleDecrypter::SetDefaultKeyId(std::string_view keyId)
{
  m_defaultKeyId = keyId;
}

void WV_CencSingleSampleDecrypter::AddKeyId(std::string_view keyId)
{
  WVSKEY key;
  key.keyid = keyId;
  key.status = cdm::KeyStatus::kUsable;

  if (std::find(keys_.begin(), keys_.end(), key) == keys_.end())
  {
    keys_.push_back(key);
  }
}

/*********************************************************************************************/

class WVDecrypter : public SSD_DECRYPTER
{
public:
  WVDecrypter() : cdmsession_(nullptr), decoding_decrypter_(nullptr) {};
  virtual ~WVDecrypter()
  {
    delete cdmsession_;
    cdmsession_ = nullptr;
  };

  virtual const char *SelectKeySytem(const char* keySystem) override
  {
    if (strcmp(keySystem, "com.widevine.alpha"))
      return nullptr;

    return "urn:uuid:EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED";
  }

  virtual bool OpenDRMSystem(const char *licenseURL, const AP4_DataBuffer &serverCertificate, const uint8_t config) override
  {
    cdmsession_ = new WV_DRM(licenseURL, serverCertificate, config);

    return cdmsession_->GetCdmAdapter() != nullptr;
  }

  virtual Adaptive_CencSingleSampleDecrypter* CreateSingleSampleDecrypter(
      AP4_DataBuffer& pssh,
      const char* optionalKeyParameter,
      std::string_view defaultkeyid,
      bool skipSessionMessage,
      CryptoMode cryptoMode) override
  {
    WV_CencSingleSampleDecrypter* decrypter =
        new WV_CencSingleSampleDecrypter(*cdmsession_, pssh, defaultkeyid, skipSessionMessage, cryptoMode);
    if (!decrypter->GetSessionId())
    {
      delete decrypter;
      decrypter = nullptr;
    }
    return decrypter;
  }

  virtual void DestroySingleSampleDecrypter(Adaptive_CencSingleSampleDecrypter* decrypter) override
  {
    if (decrypter)
    {
      // close session before dispose
      static_cast<WV_CencSingleSampleDecrypter*>(decrypter)->CloseSessionId();
      delete static_cast<WV_CencSingleSampleDecrypter*>(decrypter);
    }
  }

  virtual void GetCapabilities(Adaptive_CencSingleSampleDecrypter* decrypter, const uint8_t *keyid, uint32_t media, SSD_DECRYPTER::SSD_CAPS &caps) override
  {
    if (!decrypter)
    {
      caps = { 0,0,0 };
      return;
    }

    static_cast<WV_CencSingleSampleDecrypter*>(decrypter)->GetCapabilities(keyid, media, caps);
  }

  virtual bool HasLicenseKey(Adaptive_CencSingleSampleDecrypter* decrypter,
                             const uint8_t* keyid) override
  {
    if (decrypter)
      return static_cast<WV_CencSingleSampleDecrypter*>(decrypter)->HasKeyId(keyid);
    return false;
  }

  virtual bool HasCdmSession() override
  {
    return cdmsession_ != nullptr;
  }

  virtual std::string GetChallengeB64Data(Adaptive_CencSingleSampleDecrypter* decrypter) override
  {
    if (!decrypter)
      return "";

    AP4_DataBuffer challengeData = static_cast<WV_CencSingleSampleDecrypter*>(decrypter)->GetChallengeData();
    return BASE64::Encode(challengeData.GetData(), challengeData.GetDataSize());
  }

  virtual bool OpenVideoDecoder(Adaptive_CencSingleSampleDecrypter* decrypter,
                                const SSD_VIDEOINITDATA* initData) override
  {
    if (!decrypter || !initData)
      return false;

    decoding_decrypter_ = static_cast<WV_CencSingleSampleDecrypter*>(decrypter);
    return decoding_decrypter_->OpenVideoDecoder(initData);
  }

  virtual SSD_DECODE_RETVAL DecryptAndDecodeVideo(void* hostInstance, SSD_SAMPLE* sample) override
  {
    if (!decoding_decrypter_)
      return VC_ERROR;

    return decoding_decrypter_->DecryptAndDecodeVideo(hostInstance, sample);
  }

  virtual SSD_DECODE_RETVAL VideoFrameDataToPicture(void* hostInstance, SSD_PICTURE* picture) override
  {
    if (!decoding_decrypter_)
      return VC_ERROR;

    return decoding_decrypter_->VideoFrameDataToPicture(hostInstance, picture);
  }

  virtual void ResetVideo() override
  {
    if (decoding_decrypter_)
      decoding_decrypter_->ResetVideo();
  }

private:
  WV_DRM *cdmsession_;
  WV_CencSingleSampleDecrypter *decoding_decrypter_;
};

extern "C" {

// Linux arm64 version of libwidevinecdm.so depends on two
// dynamic symbols. See https://github.com/xbmc/inputstream.adaptive/issues/1128
#if defined(__linux__) && defined(__aarch64__) && !defined(ANDROID)
__attribute__((target("no-outline-atomics")))
int32_t __aarch64_ldadd4_acq_rel(int32_t value, int32_t *ptr)
{
  return __atomic_fetch_add(ptr, value, __ATOMIC_ACQ_REL);
}

__attribute__((target("no-outline-atomics")))
int32_t __aarch64_swp4_acq_rel(int32_t value, int32_t *ptr)
{
  return __atomic_exchange_n(ptr, value, __ATOMIC_ACQ_REL);
}
#endif

#ifdef _WIN32
#define MODULE_API __declspec(dllexport)
#else
#define MODULE_API
#endif

  SSD_DECRYPTER MODULE_API *CreateDecryptorInstance(class SSD_HOST *h, uint32_t host_version)
  {
    if (host_version != SSD_HOST::version)
      return 0;
    GLOBAL::Host = h;
    return new WVDecrypter();
  };

  void MODULE_API DeleteDecryptorInstance(SSD_DECRYPTER *d)
  {
    delete static_cast<WVDecrypter*>(d);
  }
};
