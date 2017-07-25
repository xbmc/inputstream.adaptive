/*
*      Copyright (C) 2016 liberty-developer
*      https://github.com/liberty-developer
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

#include "cdm/media/cdm/cdm_adapter.h"
#include "../src/helpers.h"
#include "../src/SSD_dll.h"
#include "jsmn.h"
#include "Ap4.h"
#include <stdarg.h>
#include <deque>

#ifndef WIDEVINECDMFILENAME
#error  "WIDEVINECDMFILENAME must be set"
#endif


SSD_HOST *host = 0;

static void Log(SSD_HOST::LOGLEVEL loglevel, const char *format, ...)
{
  char buffer[16384];
  va_list args;
  va_start(args, format);
  vsprintf(buffer, format, args);
  va_end(args);
  return host->Log(loglevel, buffer);
}

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
|   WV_CencSingleSampleDecrypter
+---------------------------------------------------------------------*/
class WV_CencSingleSampleDecrypter : public AP4_CencSingleSampleDecrypter, public media::CdmAdapterClient
{
public:
  // methods
  WV_CencSingleSampleDecrypter(std::string licenseURL, AP4_DataBuffer &pssh, AP4_DataBuffer &serverCertificate);

  bool initialized()const { return wv_adapter != 0; };

  virtual void OnCDMMessage(media::CdmAdapterClient::CDMADPMSG msg) override
  {
    Log(SSD_HOST::LL_DEBUG, "CDMMessage: %u arrived!", msg);
    messages_.push_back(msg);
  };

  virtual AP4_Result SetFrameInfo(const AP4_UI16 key_size, const AP4_UI08 *key, const AP4_UI08 nal_length_size)override;

  virtual AP4_Result DecryptSampleData(AP4_DataBuffer& data_in,
    AP4_DataBuffer& data_out,

    // always 16 bytes
    const AP4_UI08* iv,

    // pass 0 for full decryption
    unsigned int    subsample_count,

    // array of <subsample_count> integers. NULL if subsample_count is 0
    const AP4_UI16* bytes_of_cleartext_data,

    // array of <subsample_count> integers. NULL if subsample_count is 0
    const AP4_UI32* bytes_of_encrypted_data);

private:
  bool GetLicense();
  bool SendSessionMessage();

  media::CdmAdapter *wv_adapter;
  unsigned int max_subsample_count_;
  cdm::SubsampleEntry *subsample_buffer_;
  std::deque<media::CdmAdapterClient::CDMADPMSG> messages_;
  std::string pssh_, license_url_;
  AP4_UI16 key_size_;
  const AP4_UI08 *key_;
  AP4_UI08 nal_length_size_;
};

/*----------------------------------------------------------------------
|   WV_CencSingleSampleDecrypter::WV_CencSingleSampleDecrypter
+---------------------------------------------------------------------*/

WV_CencSingleSampleDecrypter::WV_CencSingleSampleDecrypter(std::string licenseURL, AP4_DataBuffer &pssh, AP4_DataBuffer &serverCertificate)
  : AP4_CencSingleSampleDecrypter(0)
  , wv_adapter(0)
  , max_subsample_count_(0)
  , subsample_buffer_(0)
  , license_url_(licenseURL)
  , pssh_(std::string(reinterpret_cast<const char*>(pssh.GetData()), pssh.GetDataSize()))
  , key_size_(0)
  , key_(0)
  , nal_length_size_(0)
{
  if (pssh.GetDataSize() > 256)
  {
    Log(SSD_HOST::LL_ERROR, "Init_data with length: %u seems not to be cenc init data!", pssh.GetDataSize());
    return;
  }

#ifdef _DEBUG
  std::string strDbg = host->GetProfilePath();
  strDbg += "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.init";
  FILE*f = fopen(strDbg.c_str(), "wb");
  fwrite(pssh_.c_str(), 1, pssh_.size(), f);
  fclose(f);
#endif

  std::string strLibPath = host->GetLibraryPath();
  if (strLibPath.empty())
  {
    Log(SSD_HOST::LL_ERROR, "Absolute path to widevine in settings expected");
    return;
  }
  strLibPath += WIDEVINECDMFILENAME;

  std::string strBasePath = host->GetProfilePath();
  char cSep = strBasePath.back();
  strBasePath += "widevine";
  strBasePath += cSep;
  host->CreateDirectory(strBasePath.c_str());
  
  //Build up a CDM path to store decrypter specific stuff. Each domain gets it own path
  const char* bspos(strchr(license_url_.c_str(), ':'));
  if (!bspos || bspos[1] != '/' || bspos[2] != '/' || !(bspos = strchr(bspos + 3, '/')))
  {
    Log(SSD_HOST::LL_ERROR, "Could not find protocol inside url - invalid");
    return;
  }
  if (bspos - license_url_.c_str() > 256)
  {
    Log(SSD_HOST::LL_ERROR, "Length of domain exeeds max. size of 256 - invalid");
    return;
  }
  char buffer[1024];
  buffer[(bspos - license_url_.c_str()) * 2] = 0;
  AP4_FormatHex(reinterpret_cast<const uint8_t*>(license_url_.c_str()), bspos - license_url_.c_str(), buffer);
  
  strBasePath += buffer;
  strBasePath += cSep;
  host->CreateDirectory(strBasePath.c_str());

  wv_adapter = new media::CdmAdapter("com.widevine.alpha", strLibPath, strBasePath, media::CdmConfig(false, true), *(dynamic_cast<media::CdmAdapterClient*>(this)));
  if (!wv_adapter->valid())
  {
    Log(SSD_HOST::LL_ERROR, "Unable to load widevine shared library (%s)", strLibPath.c_str());
    delete wv_adapter;
    wv_adapter = 0;
    return;
  }

  if (serverCertificate.GetDataSize())
    wv_adapter->SetServerCertificate(0, serverCertificate.GetData(), serverCertificate.GetDataSize());

  // For backward compatibility: If no | is found in URL, make the amazon convention out of it
  if (license_url_.find('|') == std::string::npos)
    license_url_ += "|Content-Type=application%2Fx-www-form-urlencoded|widevine2Challenge=B{SSM}&includeHdcpTestKeyInLicense=false|JBlicense";

  if (!GetLicense())
  {
    Log(SSD_HOST::LL_ERROR, "Unable to generate a license");
    delete wv_adapter;
    wv_adapter = 0;
  }
  SetParentIsOwner(false);
}

bool WV_CencSingleSampleDecrypter::GetLicense()
{
  if (strcmp(&pssh_[4], "pssh") == 0)
  {
    wv_adapter->CreateSessionAndGenerateRequest(0, cdm::SessionType::kTemporary, cdm::InitDataType::kCenc, reinterpret_cast<const uint8_t *>(pssh_.data()), pssh_.size());
  }
  else
  {
    unsigned int buf_size = 32 + pssh_.size();
    uint8_t buf[1024];

    // This will request a new session and initializes session_id and message members in cdm_adapter.
    // message will be used to create a license request in the step after CreateSession call.
    // Initialization data is the widevine cdm pssh code in google proto style found in mpd schemeIdUri
    static uint8_t proto[] = { 0x00, 0x00, 0x00, 0x63, 0x70, 0x73, 0x73, 0x68, 0x00, 0x00, 0x00, 0x00, 0xed, 0xef, 0x8b, 0xa9,
      0x79, 0xd6, 0x4a, 0xce, 0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed, 0x00, 0x00, 0x00, 0x00 };

    proto[3] = static_cast<uint8_t>(buf_size);
    proto[31] = static_cast<uint8_t>(pssh_.size());

    memcpy(buf, proto, sizeof(proto));
    memcpy(&buf[32], pssh_.data(), pssh_.size());

    wv_adapter->CreateSessionAndGenerateRequest(0, cdm::SessionType::kTemporary, cdm::InitDataType::kCenc, buf, buf_size);
  }

  //Now check messages and fire as long there is no error and messages are present.

  while (!messages_.empty())
  {
    media::CdmAdapterClient::CDMADPMSG msg = messages_.front();
    messages_.pop_front();
    switch (msg)
    {
    case media::CdmAdapterClient::kError:
      return false;
    case media::CdmAdapterClient::kSessionMessage:
      if (!SendSessionMessage())
        return false;
      break;
    default:
      ;
    }
  }

  if (!wv_adapter->KeyIdValid())
  {
    Log(SSD_HOST::LL_ERROR, "License update not successful");
    return false;
  }
  Log(SSD_HOST::LL_DEBUG, "License update successful");
  return true;
}

bool WV_CencSingleSampleDecrypter::SendSessionMessage()
{
  std::vector<std::string> headers, header, blocks = split(license_url_, '|');
  if (blocks.size() != 4)
  {
    Log(SSD_HOST::LL_ERROR, "4 '|' separated blocks in licURL expected (req / header / body / response)");
    return false;
  }

#ifdef _DEBUG
  std::string strDbg = host->GetProfilePath();
  strDbg += "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.challenge";
  FILE*f = fopen(strDbg.c_str(), "wb");
  fwrite(wv_adapter->GetMessage(), 1, wv_adapter->GetMessageSize(), f);
  fclose(f);
#endif

  //Process placeholder in GET String
  std::string::size_type insPos(blocks[0].find("{SSM}"));
  if (insPos != std::string::npos)
  {
    if (insPos >= 0 && blocks[0][insPos - 1] == 'B')
    {
      std::string msgEncoded = b64_encode(wv_adapter->GetMessage(), wv_adapter->GetMessageSize(), true);
      blocks[0].replace(insPos - 1, 6, msgEncoded);
    }
    else
    {
      Log(SSD_HOST::LL_ERROR, "Unsupported License request template (cmd)");
      return false;
    }
  }
  
  void* file = host->CURLCreate(blocks[0].c_str());
  
  size_t nbRead;
  std::string response;
  char buf[2048];

  //Set our std headers
  host->CURLAddOption(file, SSD_HOST::OPTION_PROTOCOL, "acceptencoding", "gzip, deflate");
  host->CURLAddOption(file, SSD_HOST::OPTION_PROTOCOL, "seekable", "0");
  host->CURLAddOption(file, SSD_HOST::OPTION_HEADER, "Expect", "");

  //Process headers
  headers = split(blocks[1], '&');
  for (std::vector<std::string>::iterator b(headers.begin()), e(headers.end()); b != e; ++b)
  {
    header = split(*b, '=');
    host->CURLAddOption(file, SSD_HOST::OPTION_PROTOCOL, trim(header[0]).c_str(), header.size() > 1 ? url_decode(trim(header[1])).c_str() : "");
  }

  //Process body
  if (!blocks[2].empty())
  {
    insPos = blocks[2].find("{SSM}");
    if (insPos != std::string::npos)
    {
      std::string::size_type sidSearchPos(insPos);
      if (insPos >= 0)
      {
        if (blocks[2][insPos - 1] == 'B' || blocks[2][insPos - 1] == 'b')
        {
          std::string msgEncoded = b64_encode(wv_adapter->GetMessage(), wv_adapter->GetMessageSize(), blocks[2][insPos - 1] == 'B');
          blocks[2].replace(insPos - 1, 6, msgEncoded);
          sidSearchPos += msgEncoded.size();
        }
        else
        {
          blocks[2].replace(insPos - 1, 6, reinterpret_cast<const char*>(wv_adapter->GetMessage()), wv_adapter->GetMessageSize());
          sidSearchPos += wv_adapter->GetMessageSize();
        }
      }
      else
      {
        Log(SSD_HOST::LL_ERROR, "Unsupported License request template (body)");
        goto SSMFAIL;
      }

      insPos = blocks[2].find("{SID}", sidSearchPos);
      if (insPos != std::string::npos)
      {
        if (insPos >= 0)
        {
          if (blocks[2][insPos - 1] == 'B' || blocks[2][insPos - 1] == 'b')
          {
            std::string msgEncoded = b64_encode(wv_adapter->GetSessionId(), wv_adapter->GetSessionIdSize(), blocks[2][insPos - 1] == 'B');
            blocks[2].replace(insPos - 1, 6, msgEncoded);
          }
          else
            blocks[2].replace(insPos - 1, 6, reinterpret_cast<const char*>(wv_adapter->GetSessionId()), wv_adapter->GetSessionIdSize());
        }
        else
        {
          Log(SSD_HOST::LL_ERROR, "Unsupported License request template (body)");
          goto SSMFAIL;
        }
      }
    }
    std::string decoded = b64_encode(reinterpret_cast<const unsigned char*>(blocks[2].data()), blocks[2].size(), false);
    host->CURLAddOption(file, SSD_HOST::OPTION_PROTOCOL, "postdata", decoded.c_str());
  }

  if (!host->CURLOpen(file))
  {
    Log(SSD_HOST::LL_ERROR, "License server returned failure");
    goto SSMFAIL;
  }

  // read the file
  while ((nbRead = host->ReadFile(file, buf, 1024)) > 0)
    response += std::string((const char*)buf, nbRead);

  host->CloseFile(file);
  file = 0;

  if (nbRead != 0)
  {
    Log(SSD_HOST::LL_ERROR, "Could not read full SessionMessage response");
    goto SSMFAIL;
  }

#ifdef _DEBUG
  strDbg = host->GetProfilePath();
  strDbg += "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.response";
  f = fopen(strDbg.c_str(), "wb");
  fwrite(response.c_str(), 1, response.size(), f);
  fclose(f);
#endif

  if (!blocks[3].empty())
  {
    if (blocks[3][0] == 'J')
    {
      jsmn_parser jsn;
      jsmntok_t tokens[100];

      jsmn_init(&jsn);
      int i(0), numTokens = jsmn_parse(&jsn, response.c_str(), response.size(), tokens, 100);

      for (; i < numTokens; ++i)
        if (tokens[i].type == JSMN_STRING && tokens[i].size==1
          && strncmp(response.c_str() + tokens[i].start, blocks[3].c_str() + 2, tokens[i].end - tokens[i].start)==0)
          break;

      if (i < numTokens)
      {
        if (blocks[3][1] == 'B')
        {
          unsigned int decoded_size = 2048;
          uint8_t decoded[2048];
          b64_decode(response.c_str() + tokens[i + 1].start, tokens[i + 1].end - tokens[i + 1].start, decoded, decoded_size);
          wv_adapter->UpdateSession(reinterpret_cast<const uint8_t*>(decoded), decoded_size);
        }
        else
          wv_adapter->UpdateSession(reinterpret_cast<const uint8_t*>(response.c_str() + tokens[i + 1].start), tokens[i + 1].end - tokens[i + 1].start);
      }
      else
      {
        Log(SSD_HOST::LL_ERROR, "Unable to find %s in JSON string", blocks[3].c_str() + 2);
        goto SSMFAIL;
      }
    }
    else
    {
      Log(SSD_HOST::LL_ERROR, "Unsupported License request template (response)");
      goto SSMFAIL;
    }
  } else //its binary - simply push the returned data as update
    wv_adapter->UpdateSession(reinterpret_cast<const uint8_t*>(response.data()), response.size());

  return true;
SSMFAIL:
  if (file)
    host->CloseFile(file);
  return false;
}

/*----------------------------------------------------------------------
|   WV_CencSingleSampleDecrypter::SetKeyId
+---------------------------------------------------------------------*/

AP4_Result WV_CencSingleSampleDecrypter::SetFrameInfo(const AP4_UI16 key_size, const AP4_UI08 *key, const AP4_UI08 nal_length_size)
{
  key_size_ = key_size;
  key_ = key;
  nal_length_size_ = nal_length_size;
  return AP4_SUCCESS;
}


/*----------------------------------------------------------------------
|   WV_CencSingleSampleDecrypter::DecryptSampleData
+---------------------------------------------------------------------*/
AP4_Result WV_CencSingleSampleDecrypter::DecryptSampleData(
  AP4_DataBuffer& data_in,
  AP4_DataBuffer& data_out,
  const AP4_UI08* iv,
  unsigned int    subsample_count,
  const AP4_UI16* bytes_of_cleartext_data,
  const AP4_UI32* bytes_of_encrypted_data)
{
  // the output has the same size as the input
  data_out.SetDataSize(data_in.GetDataSize());

  if (!wv_adapter)
  {
    data_out.SetData(data_in.GetData(), data_in.GetDataSize());
    return AP4_SUCCESS;
  }

  // check input parameters
  if (iv == NULL) return AP4_ERROR_INVALID_PARAMETERS;
  if (subsample_count) {
    if (bytes_of_cleartext_data == NULL || bytes_of_encrypted_data == NULL) {
      return AP4_ERROR_INVALID_PARAMETERS;
    }
  }

  // transform ap4 format into cmd format
  cdm::InputBuffer cdm_in;
  if (subsample_count > max_subsample_count_)
  {
    subsample_buffer_ = (cdm::SubsampleEntry*)realloc(subsample_buffer_, subsample_count*sizeof(cdm::SubsampleEntry));
    max_subsample_count_ = subsample_count;
  }
  for (cdm::SubsampleEntry *b(subsample_buffer_), *e(subsample_buffer_ + subsample_count); b != e; ++b, ++bytes_of_cleartext_data, ++bytes_of_encrypted_data)
  {
    b->clear_bytes = *bytes_of_cleartext_data;
    b->cipher_bytes = *bytes_of_encrypted_data;
  }
  cdm_in.data = data_in.GetData();
  cdm_in.data_size = data_in.GetDataSize();
  cdm_in.iv = iv;
  cdm_in.iv_size = 16; //Always 16, see AP4_CencSingleSampleDecrypter declaration.

  if (key_size_)
  {
    cdm_in.key_id = key_;
    cdm_in.key_id_size = key_size_;
  }
  else
  {
    cdm_in.key_id = wv_adapter->GetKeyId();
    cdm_in.key_id_size = wv_adapter->GetKeyIdSize();
  }

  cdm_in.num_subsamples = subsample_count;
  cdm_in.subsamples = subsample_buffer_;

  CdmBuffer buf(&data_out);
  CdmDecryptedBlock cdm_out;
  cdm_out.SetDecryptedBuffer(&buf);

  cdm::Status ret = wv_adapter->Decrypt(cdm_in, &cdm_out);

  return (ret == cdm::Status::kSuccess) ? AP4_SUCCESS : AP4_ERROR_INVALID_PARAMETERS;
}

class WVDecrypter: public SSD_DECRYPTER
{
public:
  // Return supported URN if type matches to capabikitues, otherwise null
  const char *Supported(const char* licenseType, const char *licenseKey) override
  {
    licenseKey_ = licenseKey;
    if (strcmp(licenseType, "com.widevine.alpha") == 0)
      return "urn:uuid:EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED";
    return 0;
  };

  AP4_CencSingleSampleDecrypter *CreateSingleSampleDecrypter(AP4_DataBuffer &streamCodec, AP4_DataBuffer &serverCertificate) override
  {
    AP4_CencSingleSampleDecrypter *res = new WV_CencSingleSampleDecrypter(licenseKey_, streamCodec, serverCertificate);
    if (!((WV_CencSingleSampleDecrypter*)res)->initialized())
    {
      delete res;
      res = 0;
    }
    return res;
  }
private:
  std::string licenseKey_;
} decrypter;

extern "C" {

#ifdef _WIN32
#define MODULE_API __declspec(dllexport)
#else
#define MODULE_API
#endif

  class SSD_DECRYPTER MODULE_API *CreateDecryptorInstance(class SSD_HOST *h, uint32_t host_version)
  {
    if (host_version != SSD_HOST::version)
      return 0;
    host = h;
    return &decrypter;
  };

};
