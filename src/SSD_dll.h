/*
 *  Copyright (C) 2017 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <stdarg.h> // va_list, va_start, va_arg, va_end
#include <string_view>

//Functionality wich is supported by the Decrypter
class Adaptive_CencSingleSampleDecrypter;
class AP4_DataBuffer;

namespace SSD
{
  // Must match to LogLevel on utils/log.h
  enum SSDLogLevel
  {
    SSDDEBUG,
    SSDINFO,
    SSDWARNING,
    SSDERROR,
    SSDFATAL
  };

  struct SSD_PICTURE;

  //Functionality wich is supported by the Addon
  class SSD_HOST
  {
  public:
    enum CURLOPTIONS
    {
      OPTION_PROTOCOL,
      OPTION_HEADER
    };
    enum CURLPROPERTY
    {
      PROPERTY_HEADER
  };
    static const uint32_t version = 19;
#if defined(ANDROID)
    virtual void* GetJNIEnv() = 0;
    virtual int GetSDKVersion() = 0;
    virtual const char *GetClassName() = 0;
#endif
    virtual const char *GetLibraryPath() const = 0;
    virtual const char *GetProfilePath() const = 0;
    virtual void* CURLCreate(const char* strURL) = 0;
    virtual bool CURLAddOption(void* file, CURLOPTIONS opt, const char* name, const char* value) = 0;
    virtual const char* CURLGetProperty(void* file, CURLPROPERTY prop, const char *name) = 0;
    virtual bool CURLOpen(void* file) = 0;
    virtual size_t ReadFile(void* file, void* lpBuf, size_t uiBufSize) = 0;
    virtual void CloseFile(void* file) = 0;
    virtual bool CreateDir(const char* dir) = 0;
    virtual bool GetBuffer(void* instance, SSD_PICTURE &picture) = 0;
    virtual void ReleaseBuffer(void* instance, void *buffer) = 0;

    virtual void LogVA(const SSDLogLevel level, const char* format, va_list args) = 0;
  };

  /*
   * Enums: SSD_VIDEOFORMAT, Codec, CodecProfile must be kept in sync with:
   * xbmc/addons/kodi-dev-kit/include/kodi/c-api/addon-instance/inputstream/stream_codec.h
   * xbmc/addons/kodi-dev-kit/include/kodi/c-api/addon-instance/video_codec.h
   */

  enum SSD_VIDEOFORMAT // refer to VIDEOCODEC_FORMAT
  {
    UnknownVideoFormat = 0,
    VideoFormatYV12,
    VideoFormatI420,
    MaxVideoFormats
  };

  enum Codec // refer to VIDEOCODEC_TYPE
  {
    CodecUnknown = 0,
    CodecVp8,
    CodecH264,
    CodecVp9,
  };

  enum CodecProfile // refer to STREAMCODEC_PROFILE
  {
    CodecProfileUnknown = 0,
    CodecProfileNotNeeded,
    H264CodecProfileBaseline,
    H264CodecProfileMain,
    H264CodecProfileExtended,
    H264CodecProfileHigh,
    H264CodecProfileHigh10,
    H264CodecProfileHigh422,
    H264CodecProfileHigh444Predictive,
    VP9CodecProfile0 = 20,
    VP9CodecProfile1,
    VP9CodecProfile2,
    VP9CodecProfile3,
  };

  struct SSD_VIDEOINITDATA
  {
    Codec codec;
    CodecProfile codecProfile;

    const SSD_VIDEOFORMAT *videoFormats;

    uint32_t width, height;

    const uint8_t *extraData;
    unsigned int extraDataSize;
  };

  struct SSD_PICTURE
  {
    enum VideoPlane {
      YPlane = 0,
      UPlane,
      VPlane,
      MaxPlanes = 3,
    };

    enum Flags : uint32_t
    {
      FLAG_NONE = 0,
      FLAG_DROP = (1 << 0),
      FLAG_DRAIN = (1 << 1),
    };

    SSD_VIDEOFORMAT videoFormat;
    uint32_t flags;

    uint32_t width, height;

    uint8_t *decodedData;
    size_t decodedDataSize;

    uint32_t planeOffsets[VideoPlane::MaxPlanes];
    uint32_t stride[VideoPlane::MaxPlanes];

    int64_t pts;

    void *buffer;
  };

  typedef struct SSD_SAMPLE
  {
    const uint8_t *data;
    uint32_t dataSize;

    int64_t pts;

    struct CRYPTO_INFO
    {
      /// @brief Number of subsamples.
      uint16_t numSubSamples;

      /// @brief Flags for later use.
      uint16_t flags;

      /// @brief @ref numSubSamples uint16_t's which define the size of clear size
      /// of a subsample.
      uint16_t* clearBytes;

      /// @brief @ref numSubSamples uint32_t's which define the size of cipher size
      /// of a subsample.
      uint32_t* cipherBytes;

      /// @brief Initialization vector
      uint8_t* iv;
      uint32_t ivSize;

      /// @brief Key id
      uint8_t* kid;
      uint32_t kidSize;

      /// @brief Encryption mode
      uint16_t mode;

      /// @brief Crypt blocks - number of blocks to encrypt in sample encryption pattern
      uint8_t cryptBlocks;

      /// @brief Skip blocks - number of blocks to skip in sample encryption pattern
      uint8_t skipBlocks;

    } cryptoInfo;
  } SSD_SAMPLE;

  enum SSD_DECODE_RETVAL
  {
    VC_NONE = 0,        //< noop
    VC_ERROR,           //< an error occured, no other messages will be returned
    VC_BUFFER,          //< the decoder needs more data
    VC_PICTURE,         //< the decoder got a picture
    VC_EOF,             //< the decoder signals EOF
  };

  class SSD_DECRYPTER
  {
  public:
    struct SSD_CAPS
    {
      static const uint32_t SSD_SUPPORTS_DECODING = 1;
      static const uint32_t SSD_SECURE_PATH = 2;
      static const uint32_t SSD_ANNEXB_REQUIRED = 4;
      static const uint32_t SSD_HDCP_RESTRICTED = 8;
      static const uint32_t SSD_SINGLE_DECRYPT = 16;
      static const uint32_t SSD_SECURE_DECODER = 32;
      static const uint32_t SSD_INVALID = 64;

      static const uint32_t SSD_MEDIA_VIDEO = 1;
      static const uint32_t SSD_MEDIA_AUDIO = 2;

      uint16_t flags;

      /* The following 2 fields are set as followed:
      - If licenseresponse return hdcp information, hdcpversion is 0 and
      hdcplimit either 0 (if hdcp is supported) or given value (if hdcpversion is not supported)
      - if no hdcp information is passed in licenseresponse, we set hdcpversion to the value we support
      manifest / representation have to check if they are allowed to be played.
      */
      uint16_t hdcpVersion; //The HDCP version streams has to be restricted 0,10,20,21,22.....
      int hdcpLimit; // If set (> 0) streams that are greater than the multiplication of "Width x Height" cannot be played.
    };

    static const uint8_t CONFIG_PERSISTENTSTORAGE = 1;

    // Return supported URN if type matches to capabilities, otherwise null
    virtual const char *SelectKeySytem(const char* keySystem) = 0;
    virtual bool OpenDRMSystem(const char *licenseURL, const AP4_DataBuffer &serverCertificate, const uint8_t config) = 0;
    virtual Adaptive_CencSingleSampleDecrypter* CreateSingleSampleDecrypter(
        AP4_DataBuffer& pssh,
        const char* optionalKeyParameter,
        std::string_view defaultkeyid,
        bool skipSessionMessage) = 0;
    virtual void DestroySingleSampleDecrypter(Adaptive_CencSingleSampleDecrypter* decrypter) = 0;

    virtual void GetCapabilities(Adaptive_CencSingleSampleDecrypter* decrypter,
                                 const uint8_t* keyid,
                                 uint32_t media,
                                 SSD_DECRYPTER::SSD_CAPS& caps) = 0;
    virtual bool HasLicenseKey(Adaptive_CencSingleSampleDecrypter* decrypter, const uint8_t *keyid) = 0;
    virtual bool HasCdmSession() = 0;
    virtual std::string GetChallengeB64Data(Adaptive_CencSingleSampleDecrypter* decrypter) = 0;

    virtual bool OpenVideoDecoder(Adaptive_CencSingleSampleDecrypter* decrypter,
                                  const SSD_VIDEOINITDATA* initData) = 0;
    virtual SSD_DECODE_RETVAL DecryptAndDecodeVideo(void* hostInstance, SSD_SAMPLE* sample) = 0;
    virtual SSD_DECODE_RETVAL VideoFrameDataToPicture(void* hostInstance, SSD_PICTURE* picture) = 0;
    virtual void ResetVideo() = 0;
  };
}; // namespace
