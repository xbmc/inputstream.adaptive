/*
 *  Copyright (C) 2018 The Chromium Authors. All rights reserved.
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 *  See LICENSES/README.md for more information.
 */

#include "CdmTypeConversion.h"

#include "utils/CryptoUtils.h"
#include "utils/log.h"

using namespace media;

std::string media::CdmStatusToString(const cdm::Status status)
{
  switch (status)
  {
    case cdm::kSuccess:
      return "kSuccess";
    case cdm::kNoKey:
      return "kNoKey";
    case cdm::kNeedMoreData:
      return "kNeedMoreData";
    case cdm::kDecryptError:
      return "kDecryptError";
    case cdm::kDecodeError:
      return "kDecodeError";
    case cdm::kInitializationError:
      return "kInitializationError";
    case cdm::kDeferredInitialization:
      return "kDeferredInitialization";
    default:
      return "Invalid Status!";
  }
}

cdm::EncryptionScheme media::ToCdmEncryptionScheme(const CryptoMode cryptoMode)
{
  switch (cryptoMode)
  {
    case CryptoMode::NONE:
      return cdm::EncryptionScheme::kUnencrypted;
    case CryptoMode::AES_CTR:
      return cdm::EncryptionScheme::kCenc;
    case CryptoMode::AES_CBC:
      return cdm::EncryptionScheme::kCbcs;
    default:
      return cdm::EncryptionScheme::kCenc;
  }
}

cdm::VideoCodec media::ToCdmVideoCodec(const VIDEOCODEC_TYPE codec)
{
  switch (codec)
  {
    case VIDEOCODEC_H264:
      return cdm::VideoCodec::kCodecH264;
    case VIDEOCODEC_VP8:
      return cdm::VideoCodec::kCodecVp8;
    case VIDEOCODEC_VP9:
      return cdm::VideoCodec::kCodecVp9;
    case VIDEOCODEC_AV1:
      return cdm::VideoCodec::kCodecAv1;
    default:
      LOG::LogF(LOGWARNING, "Unknown video codec %i", codec);
      return cdm::VideoCodec::kUnknownVideoCodec;
  }
}

cdm::VideoCodecProfile media::ToCdmVideoCodecProfile(const STREAMCODEC_PROFILE profile)
{
  switch (profile)
  {
    case STREAMCODEC_PROFILE::H264CodecProfileBaseline:
      return cdm::VideoCodecProfile::kH264ProfileBaseline;
    case STREAMCODEC_PROFILE::H264CodecProfileMain:
      return cdm::VideoCodecProfile::kH264ProfileMain;
    case STREAMCODEC_PROFILE::H264CodecProfileExtended:
      return cdm::VideoCodecProfile::kH264ProfileExtended;
    case STREAMCODEC_PROFILE::H264CodecProfileHigh:
      return cdm::VideoCodecProfile::kH264ProfileHigh;
    case STREAMCODEC_PROFILE::H264CodecProfileHigh10:
      return cdm::VideoCodecProfile::kH264ProfileHigh10;
    case STREAMCODEC_PROFILE::H264CodecProfileHigh422:
      return cdm::VideoCodecProfile::kH264ProfileHigh422;
    case STREAMCODEC_PROFILE::H264CodecProfileHigh444Predictive:
      return cdm::VideoCodecProfile::kH264ProfileHigh444Predictive;
    case STREAMCODEC_PROFILE::VP9CodecProfile0:
      return cdm::VideoCodecProfile::kVP9Profile0;
    case STREAMCODEC_PROFILE::VP9CodecProfile1:
      return cdm::VideoCodecProfile::kVP9Profile1;
    case STREAMCODEC_PROFILE::VP9CodecProfile2:
      return cdm::VideoCodecProfile::kVP9Profile2;
    case STREAMCODEC_PROFILE::VP9CodecProfile3:
      return cdm::VideoCodecProfile::kVP9Profile3;
    case STREAMCODEC_PROFILE::AV1CodecProfileMain:
      return cdm::VideoCodecProfile::kAv1ProfileMain;
    case STREAMCODEC_PROFILE::AV1CodecProfileHigh:
      return cdm::VideoCodecProfile::kAv1ProfileHigh;
    case STREAMCODEC_PROFILE::AV1CodecProfileProfessional:
      return cdm::VideoCodecProfile::kAv1ProfilePro;
    case STREAMCODEC_PROFILE::CodecProfileNotNeeded:
      return cdm::VideoCodecProfile::kProfileNotNeeded;
    default:
      LOG::LogF(LOGWARNING, "Unknown codec profile %i", profile);
      return cdm::VideoCodecProfile::kUnknownVideoCodecProfile;
  }
}

cdm::VideoFormat media::ToCdmVideoFormat(const VIDEOCODEC_FORMAT format)
{
  switch (format)
  {
    case VIDEOCODEC_FORMAT::VIDEOCODEC_FORMAT_YV12:
      return cdm::VideoFormat::kYv12;
    case VIDEOCODEC_FORMAT::VIDEOCODEC_FORMAT_I420:
      return cdm::VideoFormat::kI420;
    case VIDEOCODEC_FORMAT::VIDEOCODEC_FORMAT_YUV420P9:
      return cdm::VideoFormat::kYUV420P9;
    case VIDEOCODEC_FORMAT::VIDEOCODEC_FORMAT_YUV420P10:
      return cdm::VideoFormat::kYUV420P10;
    case VIDEOCODEC_FORMAT::VIDEOCODEC_FORMAT_YUV420P12:
      return cdm::VideoFormat::kYUV420P12;
    case VIDEOCODEC_FORMAT::VIDEOCODEC_FORMAT_YUV422P9:
      return cdm::VideoFormat::kYUV422P9;
    case VIDEOCODEC_FORMAT::VIDEOCODEC_FORMAT_YUV422P10:
      return cdm::VideoFormat::kYUV420P10;
    case VIDEOCODEC_FORMAT::VIDEOCODEC_FORMAT_YUV422P12:
      return cdm::VideoFormat::kYUV422P12;
    case VIDEOCODEC_FORMAT::VIDEOCODEC_FORMAT_YUV444P9:
      return cdm::VideoFormat::kYUV444P9;
    case VIDEOCODEC_FORMAT::VIDEOCODEC_FORMAT_YUV444P10:
      return cdm::VideoFormat::kYUV444P10;
    case VIDEOCODEC_FORMAT::VIDEOCODEC_FORMAT_YUV444P12:
      return cdm::VideoFormat::kYUV444P12;
    default:
      LOG::LogF(LOGWARNING, "Unknown video format %i", format);
      return cdm::VideoFormat::kUnknownVideoFormat;
  }
}

VIDEOCODEC_FORMAT media::ToSSDVideoFormat(const cdm::VideoFormat format)
{
  switch (format)
  {
    case cdm::VideoFormat::kYv12:
      return VIDEOCODEC_FORMAT_YV12;
    case cdm::VideoFormat::kI420:
      return VIDEOCODEC_FORMAT_I420;
    case cdm::VideoFormat::kYUV420P9:
      return VIDEOCODEC_FORMAT_YUV420P9;
    case cdm::VideoFormat::kYUV420P10:
      return VIDEOCODEC_FORMAT_YUV420P10;
    case cdm::VideoFormat::kYUV420P12:
      return VIDEOCODEC_FORMAT_YUV420P12;
    case cdm::VideoFormat::kYUV422P9:
      return VIDEOCODEC_FORMAT_YUV422P9;
    case cdm::VideoFormat::kYUV422P10:
      return VIDEOCODEC_FORMAT_YUV422P10;
    case cdm::VideoFormat::kYUV422P12:
      return VIDEOCODEC_FORMAT_YUV422P12;
    case cdm::VideoFormat::kYUV444P9:
      return VIDEOCODEC_FORMAT_YUV444P9;
    case cdm::VideoFormat::kYUV444P10:
      return VIDEOCODEC_FORMAT_YUV444P10;
    case cdm::VideoFormat::kYUV444P12:
      return VIDEOCODEC_FORMAT_YUV444P12;
    default:
      LOG::LogF(LOGWARNING, "Unknown video format %i", format);
      return VIDEOCODEC_FORMAT_UNKNOWN;
  }
}

// Warning: The returned config contains raw pointers to the extra data in the
// input |config|. Hence, the caller must make sure the input |config| outlives
// the returned config.
cdm::VideoDecoderConfig_3 media::ToCdmVideoDecoderConfig(const VIDEOCODEC_INITDATA* initData,
                                                         const CryptoMode cryptoMode)
{
  cdm::VideoDecoderConfig_3 cdmConfig{};
  cdmConfig.codec = ToCdmVideoCodec(initData->codec);
  cdmConfig.profile = ToCdmVideoCodecProfile(initData->codecProfile);

  cdmConfig.format = ToCdmVideoFormat(initData->videoFormats[0]);

  //! @todo: Color space not implemented
  cdmConfig.color_space = {2, 2, 2, cdm::ColorRange::kInvalid}; // Unspecified

  cdmConfig.coded_size.width = initData->width;
  cdmConfig.coded_size.height = initData->height;
  cdmConfig.extra_data = const_cast<uint8_t*>(initData->extraData);
  cdmConfig.extra_data_size = initData->extraDataSize;
  cdmConfig.encryption_scheme = ToCdmEncryptionScheme(cryptoMode);
  return cdmConfig;
}

void media::ToCdmInputBuffer(const DEMUX_PACKET* encryptedBuffer,
                             std::vector<cdm::SubsampleEntry>* subsamples,
                             cdm::InputBuffer_2* inputBuffer)
{
  inputBuffer->data = encryptedBuffer->pData;
  inputBuffer->data_size = encryptedBuffer->iSize;
  inputBuffer->timestamp = encryptedBuffer->pts;

  inputBuffer->key_id = encryptedBuffer->cryptoInfo->kid;
  inputBuffer->key_id_size = 16;
  inputBuffer->iv = encryptedBuffer->cryptoInfo->iv;
  inputBuffer->iv_size = 16;

  const uint16_t numSubsamples =
      encryptedBuffer->cryptoInfo ? encryptedBuffer->cryptoInfo->numSubSamples : 0;
  if (numSubsamples > 0)
  {
    subsamples->reserve(numSubsamples);
    for (uint16_t i = 0; i < numSubsamples; i++)
    {
      subsamples->push_back({encryptedBuffer->cryptoInfo->clearBytes[i],
                             encryptedBuffer->cryptoInfo->cipherBytes[i]});
    }
  }

  inputBuffer->subsamples = subsamples->data();
  inputBuffer->num_subsamples = numSubsamples;

  if (encryptedBuffer->cryptoInfo)
  {
    inputBuffer->encryption_scheme =
        ToCdmEncryptionScheme(static_cast<CryptoMode>(encryptedBuffer->cryptoInfo->mode)); // ??
  }
  else
    inputBuffer->encryption_scheme = cdm::EncryptionScheme::kUnencrypted;

  if (inputBuffer->encryption_scheme != cdm::EncryptionScheme::kUnencrypted)
  {
    inputBuffer->pattern = {encryptedBuffer->cryptoInfo->cryptBlocks,
                            encryptedBuffer->cryptoInfo->skipBlocks};
  }
}
