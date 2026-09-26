/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "DrmEngineDefines.h"

#include "utils/ResultType.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string_view>

#include <kodi/addon-instance/VideoCodec.h>
#include <kodi/addon-instance/AudioCodec.h>

class Adaptive_CencSingleSampleDecrypter;
enum class CryptoMode;

namespace DRM
{
// \brief DRM decoder
class IDecrypterDecoder
{
public:
  /*
   * \brief Open VideoCodec for decoding video in a secure pathway to Kodi
   * \param decrypter The single sample decrypter to use
   * \param initData The data for initialising the codec
   * \return True if the decoder was opened successfully otherwise false
   */
  virtual bool OpenVideoDecoder(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                const VIDEOCODEC_INITDATA* initData) = 0;

  /*
   * \brief Open AudioCodec for decoding audio in a secure pathway to Kodi
   * \param decrypter The single sample decrypter to use
   * \param initData The data for initialising the codec
   * \return True if the decoder was opened successfully otherwise false
   */
  virtual bool OpenAudioDecoder(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                const AUDIOCODEC_INITDATA* initData) = 0;

  /*
   * \brief Decrypt and decode the video packet with the supplied VideoCodec instance
   * \param codecInstance The instance of VideoCodec to use
   * \param sample The video sample/packet to decrypt and decode
   * \return Return status of the decrypt/decode action
   */
  virtual VIDEOCODEC_RETVAL DecryptAndDecodeVideo(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                  const DEMUX_PACKET* sample) = 0;

  /*
   * \brief Decrypt and decode the audio packet with the supplied AudioCodec instance
   * \param codecInstance The instance of AudioCodec to use
   * \param sample The audio sample/packet to decrypt and decode
   * \return Return status of the decrypt/decode action
   */
  virtual AUDIOCODEC_RETVAL DecryptAndDecodeAudio(kodi::addon::CInstanceAudioCodec* codecInstance,
                                                  const DEMUX_PACKET* sample) = 0;

  /*
   * \brief Convert CDM video frame data to Kodi picture format
   * \param codecInstance The instance of VideoCodec to use
   * \param picture The picture object to populate
   * \return status of the conversion
   */
  virtual VIDEOCODEC_RETVAL VideoFrameDataToPicture(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                    VIDEOCODEC_PICTURE* picture) = 0;

  /*
   * \brief Get CDM audio frame data
   * \param codecInstance The instance of AudioCodec to use
   * \param frame The frame object to populate
   * \return status of the resulting frame
   */
  virtual AUDIOCODEC_RETVAL AudioFrameDataToFrame(kodi::addon::CInstanceAudioCodec* codecInstance,
                                                  AUDIOCODEC_FRAME* frame) = 0;

  /*
   * \brief Reset the decoder
   */
  virtual void ResetVideo() = 0;

  /*
   * \brief Reset the decoder audio
   */
  virtual void ResetAudio() = 0;

  /*
   * \brief Unload decoder resources.
   */
  virtual void DisposeDecoder() {}
};

// \brief DRM System
class IDecrypter : public IDecrypterDecoder
{
public:
  static const uint8_t CONFIG_PERSISTENTSTORAGE = 1;

  virtual ~IDecrypter() {}

  /*
   * \brief Return a short name for the decrypter implementation
   */
  virtual const std::string GetName() const { return "Unknown"; }

  /*
   * \brief Initialize the decrypter library
   * \return True if has success, otherwise false
   */
  virtual bool Initialize() { return true; }

  virtual bool IsKeySystemSupported(std::string_view keySystem) = 0;

  /*
   * \brief Creates a Single Sample Decrypter for decrypting content 
   * \param initData The data for initialising the decrypter (e.g. PSSH)
   * \param defaultkeyid The default KeyID to initialise with
   * \param licenseUrl The license server URL
   * \param skipSessionMessage False for preinitialisation case
   * \param cryptoMode The crypto/cypher mode to initialise with
   * \return The single sample decrypter if successfully created
   */
  virtual std::shared_ptr<Adaptive_CencSingleSampleDecrypter> CreateSingleSampleDecrypter(
      const DRM::Config& config,
      const std::vector<uint8_t>& defaultKeyId,
      CryptoMode cryptoMode) = 0;

  /*
   * \brief Determine the capabilities of the decrypter against the supplied media type and KeyID
   * \param decrypter The single sample decrypter to use for this check
   * \param keyid The KeyID that will be used for this check
   * \param caps The capabilities object to be populated
   */
  virtual void GetCapabilities(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                               const std::vector<uint8_t>& keyId,
                               Capabilities& caps,
                               DRMMediaType mediaType) = 0;

  /*
   * \brief Check if the supplied KID has a license in the decrypter,
   *        this feature depends on implementation of decrypter, so it will return null if its not implemented.
   * \param decrypter The single sample decrypter to use for this check
   * \param keyid The KeyID to check for a valid license
   * \return True if the license contains the KID, otherwise false. Null if not implemented.
   */
  virtual std::optional<bool> HasLicenseKey(
      std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
      const std::vector<uint8_t>& keyId)
  {
    return std::nullopt;
  }

  /*
   * \brief Retrieve license challenge data
   * \param decrypter The single sample decrypter to use for license challenge
   * \return The license data in Base64 format
   */
  virtual std::string GetChallengeB64Data(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter) = 0;

  /*
   * \brief Set the auxillary library path
   * \param libraryPath Filesystem path for the decrypter to locate any needed files such as CDMs
   */
  virtual void SetLibraryPath(std::string_view libraryPath) = 0;

  /*
   * \brief Get the auxillary library path
   * \return The auxillary library path
   */
  virtual std::string_view GetLibraryPath() const = 0;
};
}; // namespace DRM
