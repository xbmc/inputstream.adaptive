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

#include "media/NdkMediaDrm.h"
#include "../src/helpers.h"
#include "../src/SSD_dll.h"
#include "jsmn.h"
#include "Ap4.h"
#include <stdarg.h>
#include <stdlib.h>
#include <deque>
#include <chrono>
#include <thread>

using namespace SSD;

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

enum WV_KEYSYSTEM
{
  NONE,
  WIDEVINE,
  PLAYREADY
};


bool needProvision = false;

void MediaDrmEventListener(AMediaDrm *media_drm, const AMediaDrmSessionId *sessionId, AMediaDrmEventType eventType, int extra, const uint8_t *data, size_t dataSize)
{
  Log(SSD_HOST::LL_DEBUG, "EVENT occured drm:%x, event:%d extra:%d dataSize;%d", (unsigned int)media_drm, eventType, extra, dataSize);
  if (eventType == EVENT_PROVISION_REQUIRED)
    needProvision = true;
}

class WV_DRM
{
public:
  WV_DRM(WV_KEYSYSTEM ks, const char* licenseURL, const AP4_DataBuffer &serverCert);
  ~WV_DRM();

  AMediaDrm *GetMediaDrm() { return media_drm_; };

  const std::string &GetLicenseURL() const { return license_url_; };

  const uint8_t *GetKeySystem() const
  {
    static const uint8_t keysystemId[2][16] = { { 0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce, 0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed },
      { 0x9A, 0x04, 0xF0, 0x79, 0x98, 0x40, 0x42, 0x86, 0xAB, 0x92, 0xE6, 0x5B, 0xE0, 0x88, 0x5F, 0x95 } };
    return keysystemId[key_system_-1];
  }
  WV_KEYSYSTEM GetKeySystemType() const { return key_system_; };

private:
  WV_KEYSYSTEM key_system_;
  AMediaDrm *media_drm_;
  std::string license_url_;
};

WV_DRM::WV_DRM(WV_KEYSYSTEM ks, const char* licenseURL, const AP4_DataBuffer &serverCert)
  : key_system_(ks)
  , media_drm_(0)
  , license_url_(licenseURL)
{
  std::string strBasePath = host->GetProfilePath();
  char cSep = strBasePath.back();
  strBasePath += ks == WIDEVINE ? "widevine" : "playready";
  strBasePath += cSep;
  host->CreateDirectory(strBasePath.c_str());

  //Build up a CDM path to store decrypter specific stuff. Each domain gets it own path
  const char* bspos(strchr(license_url_.c_str(), ':'));
  if (!bspos || bspos[1] != '/' || bspos[2] != '/' || !(bspos = strchr(bspos + 3, '/')))
  {
    Log(SSD_HOST::LL_ERROR, "Unable to find protocol inside license url - invalid");
    return;
  }
  if (bspos - license_url_.c_str() > 256)
  {
    Log(SSD_HOST::LL_ERROR, "Length of license URL exeeds max. size of 256 - invalid");
    return;
  }
  char buffer[1024];
  buffer[(bspos - license_url_.c_str()) * 2] = 0;
  AP4_FormatHex(reinterpret_cast<const uint8_t*>(license_url_.c_str()), bspos - license_url_.c_str(), buffer);

  strBasePath += buffer;
  strBasePath += cSep;
  host->CreateDirectory(strBasePath.c_str());

  media_drm_ = AMediaDrm_createByUUID(GetKeySystem());
  if (!media_drm_)
  {
    Log(SSD_HOST::LL_ERROR, "Unable to initialize media_drm");
    return;
  }

  const char* property;
  AMediaDrm_getPropertyString(media_drm_, "deviceUniqueId", &property);
  std::string strDeviceId(property? property : "unknown");
  AMediaDrm_getPropertyString(media_drm_, "securityLevel", &property);
  std::string strSecurityLevel(property? property : "unknown");

  Log(SSD_HOST::LL_DEBUG, "Successful instanciated media_drm: %X, deviceid: %s, security-level: %s", (unsigned int)media_drm_, strDeviceId.c_str(), strSecurityLevel.c_str());

  media_status_t status;
  if ((status = AMediaDrm_setOnEventListener(media_drm_, MediaDrmEventListener)) != AMEDIA_OK)
  {
    Log(SSD_HOST::LL_ERROR, "Unable to install Event Listener (%d)", status);
    AMediaDrm_release(media_drm_);
    media_drm_ = 0;
    return;
  }

  if (license_url_.find('|') == std::string::npos)
  {
    if (key_system_ == WIDEVINE)
      license_url_ += "|Content-Type=application%2Fx-www-form-urlencoded|widevine2Challenge=B{SSM}&includeHdcpTestKeyInLicense=false|JBlicense;hdcpEnforcementResolutionPixels";
    else
      license_url_ += "|Content-Type=text%2Fxml&SOAPAction=http%3A%2F%2Fschemas.microsoft.com%2FDRM%2F2007%2F03%2Fprotocols%2FAcquireLicense|R{SSM}|";
  }
}

WV_DRM::~WV_DRM()
{
  if (media_drm_)
  {
    AMediaDrm_release(media_drm_);
    media_drm_ = 0;
  }
}

/*----------------------------------------------------------------------
|   WV_CencSingleSampleDecrypter
+---------------------------------------------------------------------*/
class WV_CencSingleSampleDecrypter : public AP4_CencSingleSampleDecrypter
{
public:
  // methods
  WV_CencSingleSampleDecrypter(WV_DRM &drm, AP4_DataBuffer &pssh, const char *optionalKeyParameter);
  ~WV_CencSingleSampleDecrypter();

  size_t CreateSession(AP4_DataBuffer &pssh);
  void CloseSession(size_t sessionhandle);
  virtual const char *GetSessionId() override;
  virtual bool HasLicenseKey(const uint8_t *keyid);

  virtual AP4_Result SetFragmentInfo(AP4_UI32 pool_id, const AP4_UI08 *key, const AP4_UI08 nal_length_size, AP4_DataBuffer &annexb_sps_pps, AP4_UI32 flags)override;
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
    const AP4_UI32* bytes_of_encrypted_data);

  void GetCapabilities(const uint8_t *keyid, uint32_t media, SSD_DECRYPTER::SSD_CAPS &caps);

private:
  bool ProvisionRequest();
  bool SendSessionMessage(AMediaDrmByteArray &session_id, const uint8_t* key_request, size_t key_request_size);

  WV_DRM &media_drm_;
  std::string pssh_;

  AMediaDrmByteArray session_id_;
  char session_id_char_[128];

  const uint8_t *key_request_;
  size_t key_request_size_;

  struct FINFO
  {
    const AP4_UI08 *key_;
    AP4_UI08 nal_length_size_;
    AP4_UI16 decrypter_flags_;
    AP4_DataBuffer annexb_sps_pps_;
  };
  std::vector<FINFO> fragment_pool_;
  AP4_UI32 hdcp_limit_;
};


/*----------------------------------------------------------------------
|   WV_CencSingleSampleDecrypter::WV_CencSingleSampleDecrypter
+---------------------------------------------------------------------*/

WV_CencSingleSampleDecrypter::WV_CencSingleSampleDecrypter(WV_DRM &drm, AP4_DataBuffer &pssh, const char *optionalKeyParameter)
  : AP4_CencSingleSampleDecrypter(0)
  , media_drm_(drm)
  , key_request_(nullptr)
  , key_request_size_(0)
  , hdcp_limit_(0)
{
  SetParentIsOwner(false);

  if (pssh.GetDataSize() > 65535)
  {
    Log(SSD_HOST::LL_ERROR, "Init_data with length: %u seems not to be cenc init data!", pssh.GetDataSize());
    return;
  }

#ifdef _DEBUG
  std::string strDbg = host->GetProfilePath();
  strDbg += "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.init";
  FILE*f = fopen(strDbg.c_str(), "wb");
  fwrite(pssh.GetData(), 1, pssh.GetDataSize(), f);
  fclose(f);
#endif

  pssh_ = std::string((const char*)pssh.GetData(), pssh.GetDataSize());

  if (memcmp(pssh.GetData() + 4, "pssh", 4) != 0)
  {
    const uint8_t atomheader[] = { 0x00, 0x00, 0x00, 0x00, 0x70, 0x73, 0x73, 0x68, 0x00, 0x00, 0x00, 0x00 };
    uint8_t atom[32];
    memcpy(atom, atomheader, 12);
    memcpy(atom+12, media_drm_.GetKeySystem(), 16);
    memset(atom+28, 0, 4);

    pssh_.insert(0, std::string(reinterpret_cast<const char*>(atom), sizeof(atom)));

    pssh_[3] = static_cast<uint8_t>(pssh_.size());
    pssh_[2] = static_cast<uint8_t>(pssh_.size() >> 8);

    pssh_[sizeof(atom) - 1] = static_cast<uint8_t>(pssh_.size()) - sizeof(atom);
    pssh_[sizeof(atom) - 2] = static_cast<uint8_t>((pssh_.size() - sizeof(atom)) >> 8);
  }

  media_status_t status;

  bool retry(false);
  size_t old_key_request_size(0);

SESSIONAGIN:
  memset(&session_id_, 0, sizeof(session_id_));
  if ((status = AMediaDrm_openSession(media_drm_.GetMediaDrm(), &session_id_)) != AMEDIA_OK)
  {
    Log(SSD_HOST::LL_ERROR, "Unable to open DRM session (%d)", status);
    return;
  }

TRYAGAIN:
  if (needProvision && !ProvisionRequest())
  {
    Log(SSD_HOST::LL_ERROR, "Unable to generate a license (provision failed)");
    goto FAILWITHSESSION;
  }
  needProvision = false;

  AMediaDrmKeyValuePair kv;
  kv.mKey = "PRCustomData";
  kv.mValue = optionalKeyParameter;

  status = AMediaDrm_getKeyRequest(media_drm_.GetMediaDrm(), &session_id_,
    reinterpret_cast<const uint8_t*>(pssh_.data()), pssh_.size(), "video/mp4", KEY_TYPE_STREAMING,
    &kv, (optionalKeyParameter != nullptr) ? 1 : 0 , &key_request_, &key_request_size_);

  if (status != AMEDIA_OK || !key_request_size_)
  {
    Log(SSD_HOST::LL_ERROR, "Key request not successful (%d)", status);
    if (needProvision)
      goto TRYAGAIN;
    else
      goto FAILWITHSESSION;
  }

  Log(SSD_HOST::LL_DEBUG, "Key request successful, size: %u, oldsize: %u", reinterpret_cast<unsigned int>(key_request_size_), reinterpret_cast<unsigned int>(old_key_request_size));

  if (!SendSessionMessage(session_id_, key_request_, key_request_size_ - old_key_request_size))
    goto FAILWITHSESSION;

  Log(SSD_HOST::LL_DEBUG, "License update successful");

  if (retry)
  {
    retry = false;
    old_key_request_size = key_request_size_;
    goto SESSIONAGIN;
  }

  memcpy(session_id_char_, session_id_.ptr, session_id_.length);
  session_id_char_[session_id_.length] = 0;

  return;


FAILWITHSESSION:
  //AMediaDrm_closeSession(media_drm_.GetMediaDrm(), &session_id_);
  memset(&session_id_, 0, sizeof(session_id_));

  return;
}

WV_CencSingleSampleDecrypter::~WV_CencSingleSampleDecrypter()
{
}

const char *WV_CencSingleSampleDecrypter::GetSessionId()
{
  return session_id_char_;
}

bool WV_CencSingleSampleDecrypter::HasLicenseKey(const uint8_t *keyid)
{
  // We work with one session for all streams.
  // All license keys must be given in this key request
  return true;
}

void WV_CencSingleSampleDecrypter::GetCapabilities(const uint8_t *keyid, uint32_t media, SSD_DECRYPTER::SSD_CAPS &caps)
{
  caps = { SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH | SSD_DECRYPTER::SSD_CAPS::SSD_ANNEXB_REQUIRED, 0, hdcp_limit_ };

  const char *property;
  AMediaDrm_getPropertyString(media_drm_.GetMediaDrm(), "securityLevel", &property);
  if (property && strcmp(property, "L1") == 0)
  {
    caps.hdcpLimit = 0; //No restriction
    caps.flags |= SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_DECODER;
  }
  Log(SSD_HOST::LL_DEBUG, "GetCapabilities: hdcpLimit: %u", caps.hdcpLimit);
}

bool WV_CencSingleSampleDecrypter::ProvisionRequest()
{
  const char *url(0);
  size_t prov_size(4096);

  Log(SSD_HOST::LL_ERROR, "PrivisionData request: drm: %x key_request_size_: %u", (unsigned int)media_drm_.GetMediaDrm(), key_request_size_);

  media_status_t status = AMediaDrm_getProvisionRequest(media_drm_.GetMediaDrm(), &key_request_, &prov_size, &url);

  if (status != AMEDIA_OK || !url)
  {
    Log(SSD_HOST::LL_ERROR, "PrivisionData request failed with status: %d", status);
    return false;
  }
  Log(SSD_HOST::LL_DEBUG, "PrivisionData: status: %d, size: %u, url: %s", status, prov_size, url);

  std::string tmp_str("{\"signedRequest\":\"");
  tmp_str += std::string(reinterpret_cast<const char*>(key_request_), prov_size);
  tmp_str += "\"}";

  std::string encoded = b64_encode(reinterpret_cast<const unsigned char*>(tmp_str.data()), tmp_str.size(), false);

  void* file = host->CURLCreate(url);
  host->CURLAddOption(file, SSD_HOST::OPTION_PROTOCOL, "Content-Type", "application/json");
  host->CURLAddOption(file, SSD_HOST::OPTION_PROTOCOL, "seekable", "0");
  host->CURLAddOption(file, SSD_HOST::OPTION_PROTOCOL, "postdata", encoded.c_str());

  if (!host->CURLOpen(file))
  {
    Log(SSD_HOST::LL_ERROR, "Provisioning server returned failure");
    return false;
  }
  tmp_str.clear();
  char buf[8192];
  size_t nbRead;

  // read the file
  while ((nbRead = host->ReadFile(file, buf, 8192)) > 0)
    tmp_str += std::string((const char*)buf, nbRead);

  status = AMediaDrm_provideProvisionResponse(media_drm_.GetMediaDrm(), reinterpret_cast<const uint8_t *>(tmp_str.c_str()), tmp_str.size());

  Log(SSD_HOST::LL_DEBUG, "provideProvisionResponse: status %d", status);
  return status == AMEDIA_OK;;
}

bool WV_CencSingleSampleDecrypter::SendSessionMessage(AMediaDrmByteArray &session_id, const uint8_t* key_request, size_t key_request_size)
{
  std::vector<std::string> headers, header, blocks = split(media_drm_.GetLicenseURL(), '|');
  if (blocks.size() != 4)
  {
    Log(SSD_HOST::LL_ERROR, "4 '|' separated blocks in licURL expected (req / header / body / response)");
    return false;
  }

#ifdef _DEBUG
  std::string strDbg = host->GetProfilePath();
  strDbg += "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.challenge";
  FILE*f = fopen(strDbg.c_str(), "wb");
  fwrite(key_request, 1, key_request_size, f);
  fclose(f);
#endif

  //Process placeholder in GET String
  std::string::size_type insPos(blocks[0].find("{SSM}"));
  if (insPos != std::string::npos)
  {
    if (insPos >= 0 && blocks[0][insPos - 1] == 'B')
    {
      std::string msgEncoded = b64_encode(key_request, key_request_size, true);
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
  AMediaDrmKeySetId dummy_ksid; //STREAMING returns 0
  media_status_t status;
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
          std::string msgEncoded = b64_encode(key_request, key_request_size, blocks[2][insPos - 1] == 'B');
          blocks[2].replace(insPos - 1, 6, msgEncoded);
        }
        else
          blocks[2].replace(insPos - 1, 6, reinterpret_cast<const char*>(key_request), key_request_size);
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
            std::string msgEncoded = b64_encode(session_id.ptr, session_id.length, blocks[2][insPos - 1] == 'B');
            blocks[2].replace(insPos - 1, 6, msgEncoded);
          }
          else
            blocks[2].replace(insPos - 1, 6, reinterpret_cast<const char*>(session_id.ptr), session_id.length);
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

  if (media_drm_.GetKeySystemType() == PLAYREADY && response.find("<LicenseNonce>") == std::string::npos)
  {
    std::string::size_type dstPos(response.find("</Licenses>"));
    std::string challenge((const char*)key_request, key_request_size);
    std::string::size_type srcPosS(challenge.find("<LicenseNonce>"));
    if (dstPos != std::string::npos && srcPosS != std::string::npos)
    {
      Log(SSD_HOST::LL_DEBUG, "Inserting <LicenseNonce>");
      std::string::size_type srcPosE(challenge.find("</LicenseNonce>", srcPosS));
      if (srcPosE != std::string::npos)
        response.insert(dstPos + 11, challenge.c_str() + srcPosS, srcPosE - srcPosS + 15);
    }
  }

#ifdef _DEBUG
  strDbg = host->GetProfilePath();
  strDbg += "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.response";
  f = fopen(strDbg.c_str(), "wb");
  fwrite(response.data(), 1, response.size(), f);
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

      std::vector<std::string> jsonVals = split(blocks[3].c_str()+2, ';');

      // Find HDCP limit
      if (jsonVals.size() > 1)
      {
        for (; i < numTokens; ++i)
          if (tokens[i].type == JSMN_STRING && tokens[i].size == 1 && jsonVals[1].size() == tokens[i].end - tokens[i].start
            && strncmp(response.c_str() + tokens[i].start, jsonVals[1].c_str(), tokens[i].end - tokens[i].start) == 0)
            break;
        if (i < numTokens)
          hdcp_limit_ = atoi((response.c_str() + tokens[i + 1].start));
      }
      // Find license key
      if (jsonVals.size() > 0)
      {
        for (i = 0; i < numTokens; ++i)
          if (tokens[i].type == JSMN_STRING && tokens[i].size == 1 && jsonVals[0].size() == tokens[i].end - tokens[i].start
            && strncmp(response.c_str() + tokens[i].start, jsonVals[0].c_str(), tokens[i].end - tokens[i].start) == 0)
            break;
      }
      else
        i = numTokens;

      if (i < numTokens)
      {
        if (blocks[3][1] == 'B')
        {
          unsigned int decoded_size = 2048;
          uint8_t decoded[2048];

          b64_decode(response.c_str() + tokens[i + 1].start, tokens[i + 1].end - tokens[i + 1].start, decoded, decoded_size);
          status = AMediaDrm_provideKeyResponse(media_drm_.GetMediaDrm(), &session_id, decoded, decoded_size, &dummy_ksid);
        }
        else
          status = AMediaDrm_provideKeyResponse(media_drm_.GetMediaDrm(), &session_id, reinterpret_cast<const uint8_t*>(response.c_str() + tokens[i + 1].start), tokens[i + 1].end - tokens[i + 1].start, &dummy_ksid);
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
  }
  else //its binary - simply push the returned data as update
    status = AMediaDrm_provideKeyResponse(media_drm_.GetMediaDrm(), &session_id, reinterpret_cast<const uint8_t*>(response.data()), response.size(), &dummy_ksid);

  return status == AMEDIA_OK;
SSMFAIL:
  if (file)
    host->CloseFile(file);
  return false;
}

/*----------------------------------------------------------------------
|   WV_CencSingleSampleDecrypter::SetKeyId
+---------------------------------------------------------------------*/

AP4_Result WV_CencSingleSampleDecrypter::SetFragmentInfo(AP4_UI32 pool_id, const AP4_UI08 *key,
  const AP4_UI08 nal_length_size, AP4_DataBuffer &annexb_sps_pps, AP4_UI32 flags)
{
  if (pool_id >= fragment_pool_.size())
    return AP4_ERROR_OUT_OF_RANGE;

  fragment_pool_[pool_id].key_ = key;
  fragment_pool_[pool_id].nal_length_size_ = nal_length_size;
  fragment_pool_[pool_id].annexb_sps_pps_.SetData(annexb_sps_pps.GetData(), annexb_sps_pps.GetDataSize());
  fragment_pool_[pool_id].decrypter_flags_ = flags;

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
  if (!media_drm_.GetMediaDrm())
    return AP4_ERROR_INVALID_STATE;

  if (data_in.GetDataSize() > 0)
  {
    FINFO &fragInfo(fragment_pool_[pool_id]);

    if (fragInfo.nal_length_size_ > 4)
    {
      Log(SSD_HOST::LL_ERROR, "Nalu length size > 4 not supported");
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
      //Note that we assume that there is enough data in data_out to hold everything without reallocating.

      //check NAL / subsample
      const AP4_Byte *packet_in(data_in.GetData()), *packet_in_e(data_in.GetData() + data_in.GetDataSize());
      AP4_Byte *packet_out(data_out.UseData() + data_out.GetDataSize());
      AP4_UI16 *clrb_out(iv ? reinterpret_cast<AP4_UI16*>(data_out.UseData() + sizeof(subsample_count)) : nullptr);
      unsigned int nalunitcount(0), nalunitsum(0), configSize(0);

      while (packet_in < packet_in_e)
      {
        uint32_t nalsize(0);
        for (unsigned int i(0); i < fragInfo.nal_length_size_; ++i) { nalsize = (nalsize << 8) + *packet_in++; };

        //look if we have to inject sps / pps
        if (fragInfo.annexb_sps_pps_.GetDataSize() && (*packet_in & 0x1F) != 9 /*AVC_NAL_AUD*/)
        {
          memcpy(packet_out, fragInfo.annexb_sps_pps_.GetData(), fragInfo.annexb_sps_pps_.GetDataSize());
          packet_out += fragInfo.annexb_sps_pps_.GetDataSize();
          if (clrb_out) *clrb_out += fragInfo.annexb_sps_pps_.GetDataSize();
          configSize = fragInfo.annexb_sps_pps_.GetDataSize();
          fragInfo.annexb_sps_pps_.SetDataSize(0);
        }

        //Anex-B Start pos
        packet_out[0] = packet_out[1] = packet_out[2] = 0; packet_out[3] = 1;
        packet_out += 4;
        memcpy(packet_out, packet_in, nalsize);
        packet_in += nalsize;
        packet_out += nalsize;
        if (clrb_out) *clrb_out += (4 - fragInfo.nal_length_size_);
        ++nalunitcount;

        if (nalsize + fragInfo.nal_length_size_ + nalunitsum > *bytes_of_cleartext_data + *bytes_of_encrypted_data)
        {
          Log(SSD_HOST::LL_ERROR, "NAL Unit exceeds subsample definition (nls: %d) %d -> %d ", fragInfo.nal_length_size_,
            nalsize + fragInfo.nal_length_size_ + nalunitsum, *bytes_of_cleartext_data + *bytes_of_encrypted_data);
          return AP4_ERROR_NOT_SUPPORTED;
        }
        else if (!iv)
        {
          nalunitsum = 0;
        }
        else if (nalsize + fragInfo.nal_length_size_ + nalunitsum == *bytes_of_cleartext_data + *bytes_of_encrypted_data)
        {
          ++bytes_of_cleartext_data;
          ++bytes_of_encrypted_data;
          ++clrb_out;
          --subsample_count;
          nalunitsum = 0;
        }
        else
          nalunitsum += nalsize + fragInfo.nal_length_size_;
      }
      if (packet_in != packet_in_e || subsample_count)
      {
        Log(SSD_HOST::LL_ERROR, "NAL Unit definition incomplete (nls: %d) %d -> %u ", fragInfo.nal_length_size_, (int)(packet_in_e - packet_in), subsample_count);
        return AP4_ERROR_NOT_SUPPORTED;
      }
      data_out.SetDataSize(data_out.GetDataSize() + data_in.GetDataSize() + configSize + (4 - fragInfo.nal_length_size_) * nalunitcount);
    }
    else
    {
      data_out.AppendData(data_in.GetData(), data_in.GetDataSize());
      fragInfo.annexb_sps_pps_.SetDataSize(0);
    }
  }
  else
    data_out.SetDataSize(0);
  return AP4_SUCCESS;
}

class WVDecrypter : public SSD_DECRYPTER
{
public:
  WVDecrypter() : key_system_(NONE), cdmsession_(nullptr) {};
  ~WVDecrypter()
  {
    delete cdmsession_;
    cdmsession_ = nullptr;
  };

  virtual const char *SelectKeySytem(const char* keySystem)
  {
    Log(SSD_HOST::LL_ERROR, "Key system request: %s", keySystem);
    if (strcmp(keySystem, "com.widevine.alpha") == 0)
    {
      key_system_ = WIDEVINE;
      return "urn:uuid:EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED";
    }
    else if (strcmp(keySystem, "com.microsoft.playready") == 0)
    {
      key_system_ = PLAYREADY;
      return "urn:uuid:9A04F079-9840-4286-AB92-E65BE0885F95";
    }
    else
      return nullptr;
  }

  virtual bool OpenDRMSystem(const char *licenseURL, const AP4_DataBuffer &serverCertificate)
  {
    if (key_system_ == NONE)
      return false;

    cdmsession_ = new WV_DRM(key_system_, licenseURL, serverCertificate);

    return cdmsession_->GetMediaDrm();
  }

  virtual AP4_CencSingleSampleDecrypter *CreateSingleSampleDecrypter(AP4_DataBuffer &pssh, const char *optionalKeyParameter) override
  {
    WV_CencSingleSampleDecrypter *decrypter = new WV_CencSingleSampleDecrypter(*cdmsession_, pssh, optionalKeyParameter);
    if (!decrypter->GetSessionId())
    {
      delete decrypter;
      decrypter = nullptr;
    }
    return decrypter;
  }

  virtual void DestroySingleSampleDecrypter(AP4_CencSingleSampleDecrypter* decrypter) override
  {
    if (decrypter)
      delete static_cast<WV_CencSingleSampleDecrypter*>(decrypter);
  }

  virtual void GetCapabilities(AP4_CencSingleSampleDecrypter* decrypter, const uint8_t *keyid, uint32_t media, SSD_DECRYPTER::SSD_CAPS &caps) override
  {
    if (decrypter)
      static_cast<WV_CencSingleSampleDecrypter*>(decrypter)->GetCapabilities(keyid,media,caps);
    else
      caps = { 0, 0, 0};
  }

  virtual bool HasLicenseKey(AP4_CencSingleSampleDecrypter* decrypter, const uint8_t *keyid)
  {
    if (decrypter)
      return static_cast<WV_CencSingleSampleDecrypter*>(decrypter)->HasLicenseKey(keyid);
    return false;
  }

  virtual bool OpenVideoDecoder(AP4_CencSingleSampleDecrypter* decrypter, const SSD_VIDEOINITDATA *initData)
  {
    return false;
  }

  virtual SSD_DECODE_RETVAL DecodeVideo(void* hostInstance, SSD_SAMPLE *sample, SSD_PICTURE *picture) override
  {
    return VC_ERROR;
  }

  virtual void ResetVideo() override
  {
  }

private:
  WV_KEYSYSTEM key_system_;
  WV_DRM *cdmsession_;
};

extern "C" {

#ifdef _WIN32
#define MODULE_API __declspec(dllexport)
#else
#define MODULE_API
#endif

  SSD_DECRYPTER MODULE_API *CreateDecryptorInstance(class SSD_HOST *h, uint32_t host_version)
  {
    if (host_version != SSD_HOST::version)
      return 0;
    host = h;
    return new WVDecrypter;
  };

  void MODULE_API DeleteDecryptorInstance(class SSD_DECRYPTER *d)
  {
    delete static_cast<WVDecrypter*>(d);
  }
};
