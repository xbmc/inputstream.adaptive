/*
 *  Copyright (C) 2018 The Chromium Authors. All rights reserved.
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 *  See LICENSES/README.md for more information.
 */

#include "cdm_type_conversion.h"
#include "../../../Helper.h"

using namespace SSD;

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

cdm::VideoCodec media::ToCdmVideoCodec(const SSD::Codec codec)
{
  switch (codec)
  {
    case SSD::Codec::CodecH264:
      return cdm::VideoCodec::kCodecH264;
    case SSD::Codec::CodecVp8:
      return cdm::VideoCodec::kCodecVp8;
    case SSD::Codec::CodecVp9:
      return cdm::VideoCodec::kCodecVp9;
    default:
      LOG::LogF(SSDWARNING, "Unknown video codec %i", codec);
      return cdm::VideoCodec::kUnknownVideoCodec;
  }
}

cdm::VideoCodecProfile media::ToCdmVideoCodecProfile(const SSD::CodecProfile profile)
{
  switch (profile)
  {
    case SSD::CodecProfile::H264CodecProfileBaseline:
      return cdm::VideoCodecProfile::kH264ProfileBaseline;
    case SSD::CodecProfile::H264CodecProfileMain:
      return cdm::VideoCodecProfile::kH264ProfileMain;
    case SSD::CodecProfile::H264CodecProfileExtended:
      return cdm::VideoCodecProfile::kH264ProfileExtended;
    case SSD::CodecProfile::H264CodecProfileHigh:
      return cdm::VideoCodecProfile::kH264ProfileHigh;
    case SSD::CodecProfile::H264CodecProfileHigh10:
      return cdm::VideoCodecProfile::kH264ProfileHigh10;
    case SSD::CodecProfile::H264CodecProfileHigh422:
      return cdm::VideoCodecProfile::kH264ProfileHigh422;
    case SSD::CodecProfile::H264CodecProfileHigh444Predictive:
      return cdm::VideoCodecProfile::kH264ProfileHigh444Predictive;
    case SSD::CodecProfile::VP9CodecProfile0:
      return cdm::VideoCodecProfile::kVP9Profile0;
    case SSD::CodecProfile::VP9CodecProfile1:
      return cdm::VideoCodecProfile::kVP9Profile1;
    case SSD::CodecProfile::VP9CodecProfile2:
      return cdm::VideoCodecProfile::kVP9Profile2;
    case SSD::CodecProfile::VP9CodecProfile3:
      return cdm::VideoCodecProfile::kVP9Profile3;
    case SSD::CodecProfile::CodecProfileNotNeeded:
      return cdm::VideoCodecProfile::kProfileNotNeeded;
    default:
      LOG::LogF(SSDWARNING, "Unknown codec profile %i", profile);
      return cdm::VideoCodecProfile::kUnknownVideoCodecProfile;
  }
}

cdm::VideoFormat media::ToCdmVideoFormat(const SSD::SSD_VIDEOFORMAT format)
{
  switch (format)
  {
    case SSD::SSD_VIDEOFORMAT::VideoFormatYV12:
      return cdm::VideoFormat::kYv12;
    case SSD::SSD_VIDEOFORMAT::VideoFormatI420:
      return cdm::VideoFormat::kI420;
    default:
      LOG::LogF(SSDWARNING, "Unknown video format %i", format);
      return cdm::VideoFormat::kUnknownVideoFormat;
  }
}

SSD::SSD_VIDEOFORMAT media::ToSSDVideoFormat(const cdm::VideoFormat format)
{
  switch (format)
  {
    case cdm::VideoFormat::kYv12:
      return SSD::SSD_VIDEOFORMAT::VideoFormatYV12;
    case cdm::VideoFormat::kI420:
      return SSD::SSD_VIDEOFORMAT::VideoFormatI420;
    default:
      LOG::LogF(SSDWARNING, "Unknown video format %i", format);
      return SSD::SSD_VIDEOFORMAT::UnknownVideoFormat;
  }
}

// Warning: The returned config contains raw pointers to the extra data in the
// input |config|. Hence, the caller must make sure the input |config| outlives
// the returned config.
cdm::VideoDecoderConfig_3 media::ToCdmVideoDecoderConfig(const SSD::SSD_VIDEOINITDATA* initData,
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

void media::ToCdmInputBuffer(const SSD::SSD_SAMPLE* encryptedBuffer,
                             std::vector<cdm::SubsampleEntry>* subsamples,
                             cdm::InputBuffer_2* inputBuffer)
{
  inputBuffer->data = encryptedBuffer->data;
  inputBuffer->data_size = encryptedBuffer->dataSize;
  inputBuffer->timestamp = encryptedBuffer->pts;

  inputBuffer->key_id = encryptedBuffer->cryptoInfo.kid;
  inputBuffer->key_id_size = encryptedBuffer->cryptoInfo.kidSize;
  inputBuffer->iv = encryptedBuffer->cryptoInfo.iv;
  inputBuffer->iv_size = encryptedBuffer->cryptoInfo.ivSize;

  const uint16_t numSubsamples = encryptedBuffer->cryptoInfo.numSubSamples;
  if (numSubsamples > 0)
  {
    subsamples->reserve(numSubsamples);
    for (uint16_t i = 0; i < numSubsamples; i++)
    {
      subsamples->push_back(
          {encryptedBuffer->cryptoInfo.clearBytes[i], encryptedBuffer->cryptoInfo.cipherBytes[i]});
    }
  }

  inputBuffer->subsamples = subsamples->data();
  inputBuffer->num_subsamples = numSubsamples;

  inputBuffer->encryption_scheme =
      ToCdmEncryptionScheme(static_cast<CryptoMode>(encryptedBuffer->cryptoInfo.mode));

  if (inputBuffer->encryption_scheme != cdm::EncryptionScheme::kUnencrypted)
  {
    inputBuffer->pattern = {encryptedBuffer->cryptoInfo.cryptBlocks,
                            encryptedBuffer->cryptoInfo.skipBlocks};
  }
}
