/*
 *  Copyright (C) 2018 The Chromium Authors. All rights reserved.
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cdm/media/cdm/api/content_decryption_module.h"

#include <string>
#include <vector>

#include <kodi/addon-instance/VideoCodec.h>

enum class CryptoMode;

namespace media
{

// CDM Converters

std::string CdmStatusToString(const cdm::Status status);

cdm::EncryptionScheme ToCdmEncryptionScheme(const CryptoMode cryptoMode);

cdm::VideoCodec ToCdmVideoCodec(const VIDEOCODEC_TYPE codec);

cdm::VideoCodecProfile ToCdmVideoCodecProfile(const STREAMCODEC_PROFILE profile);

// Video Converters

cdm::VideoFormat ToCdmVideoFormat(const VIDEOCODEC_FORMAT videoFormat);

VIDEOCODEC_FORMAT ToSSDVideoFormat(const cdm::VideoFormat format);

// Aggregated Types

// Warning: The returned config contains raw pointers to the extra data in the
// input |config|. Hence, the caller must make sure the input |config| outlives
// the returned config.
cdm::VideoDecoderConfig_3 ToCdmVideoDecoderConfig(const VIDEOCODEC_INITDATA* initData,
                                                  const CryptoMode cryptoMode);

// Fill |input_buffer| based on the values in |encrypted|. |subsamples|
// is used to hold some of the data. |input_buffer| will contain pointers
// to data contained in |encrypted| and |subsamples|, so the lifetime of
// |input_buffer| must be <= the lifetime of |encrypted| and |subsamples|.
void ToCdmInputBuffer(const DEMUX_PACKET* encryptedBuffer,
                      std::vector<cdm::SubsampleEntry>* subsamples,
                      cdm::InputBuffer_2* inputBuffer);

} // namespace media
