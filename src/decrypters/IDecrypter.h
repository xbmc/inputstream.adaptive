/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string_view>

#include <kodi/addon-instance/VideoCodec.h>

class Adaptive_CencSingleSampleDecrypter;
enum class CryptoMode;

namespace DRM
{
struct DecrypterCapabilites
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

  uint16_t flags{0};

  /* The following 2 fields are set as followed:
     - If licenseresponse return hdcp information, hdcpversion is 0 and
       hdcplimit either 0 (if hdcp is supported) or given value (if hdcpversion is not supported)
     - if no hdcp information is passed in licenseresponse, we set hdcpversion to the value we support
       manifest / representation have to check if they are allowed to be played.
  */
  uint16_t hdcpVersion{0}; //The HDCP version streams has to be restricted 0,10,20,21,22.....
  int hdcpLimit{0}; // If set (> 0) streams that are greater than the multiplication of "Width x Height" cannot be played.
};

struct Config
{
  // To enable persistent state CDM behaviour
  bool isPersistentStorage{false};
  // Optional parameters to make the CDM key request (CDM specific parameters)
  std::map<std::string, std::string> optKeyReqParams;

  struct License
  {
    // The license server certificate
    std::vector<uint8_t> serverCert;
    // The license server url
    std::string serverUrl;
    // To force an HTTP GET request, instead that POST request
    bool isHttpGetRequest{false};
    // HTTP request headers
    std::map<std::string, std::string> reqHeaders;
    // HTTP parameters to append to the url
    std::string reqParams;
    // Custom license data encoded as base64 to make the HTTP license request
    std::string reqData;
    // License data wrappers
    // Multiple wrappers supported e.g. "base64,json", the name order defines the order
    // in which data will be wrapped, (1) base64 --> (2) url
    std::string wrapper;
    // License data unwrappers
    // Multiple un-wrappers supported e.g. "base64,json", the name order defines the order
    // in which data will be unwrapped, (1) base64 --> (2) json
    std::string unwrapper;
    // License data unwrappers parameters
    std::map<std::string, std::string> unwrapperParams;
    // Clear key's for ClearKey DRM (KID / KEY pair)
    std::map<std::string, std::string> keys;
  };

  // The license configuration
  License license;
  // Specifies if has been parsed the new DRM config ("drm" or "drm_legacy" kodi property)
  //! @todo: to remove when deprecated DRM properties will be removed
  bool isNewConfig{true};
};

class IDecrypter
{
public:
  static const uint8_t CONFIG_PERSISTENTSTORAGE = 1;

  virtual ~IDecrypter(){};

  /**
   * \brief Initialize the decrypter library
   * \return True if has success, otherwise false
   */
  virtual bool Initialize() { return true; }

  /**
   * \brief Used to ensure the correct key system is selected
   * \param keySystem The URN to be matched
   * \return Supported URN if type matches to capabilities, otherwise null
   */
  virtual std::vector<std::string_view> SelectKeySystems(std::string_view keySystem) = 0;

  /**
   * \brief Initialise the DRM system
   * \param config The DRM configuration
   * \return true on success 
   */
  virtual bool OpenDRMSystem(const DRM::Config& config) = 0;
  
  /**
   * \brief Creates a Single Sample Decrypter for decrypting content 
   * \param initData The data for initialising the decrypter (e.g. PSSH)
   * \param defaultkeyid The default KeyID to initialise with
   * \param licenseUrl The license server URL
   * \param skipSessionMessage False for preinitialisation case
   * \param cryptoMode The crypto/cypher mode to initialise with
   * \return The single sample decrypter if successfully created
   */
  virtual std::shared_ptr<Adaptive_CencSingleSampleDecrypter> CreateSingleSampleDecrypter(
      std::vector<uint8_t>& initData,
      const std::vector<uint8_t>& defaultKeyId,
      std::string_view licenseUrl,
      bool skipSessionMessage,
      CryptoMode cryptoMode) = 0;

  /**
   * \brief Determine the capabilities of the decrypter against the supplied media type and KeyID
   * \param decrypter The single sample decrypter to use for this check
   * \param keyid The KeyID that will be used for this check
   * \param media The type of media being decrypted (audio/video)
   * \param caps The capabilities object to be populated
   */
  virtual void GetCapabilities(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                               const std::vector<uint8_t>& keyId,
                               uint32_t media,
                               DecrypterCapabilites& caps) = 0;

  /**
   * \brief Check if the supplied KeyID has a license in the decrypter
   * \param decrypter The single sample decrypter to use for this check
   * \param keyid The KeyID to check for a valid license
   * \return True if the KeyID has a license otherwise false
   */
  virtual bool HasLicenseKey(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                             const std::vector<uint8_t>& keyId) = 0;

  /**
   * \brief Check if the decrypter has been initialised (OpenDRMSystem called)
   * \return True if decrypter has been initialised otherwise false
   */
  virtual bool IsInitialised() = 0;

  /**
   * \brief Retrieve license challenge data
   * \param decrypter The single sample decrypter to use for license challenge
   * \return The license data in Base64 format
   */
  virtual std::string GetChallengeB64Data(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter) = 0;

  /**
   * \brief Open VideoCodec for decoding video in a secure pathway to Kodi
   * \param decrypter The single sample decrypter to use
   * \param initData The data for initialising the codec
   * \return True if the decoder was opened successfully otherwise false
   */
  virtual bool OpenVideoDecoder(std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter,
                                const VIDEOCODEC_INITDATA* initData) = 0;

  /**
   * \brief Decrypt and decode the video packet with the supplied VideoCodec instance
   * \param codecInstance The instance of VideoCodec to use
   * \param sample The video sample/packet to decrypt and decode
   * \return Return status of the decrypt/decode action
   */
  virtual VIDEOCODEC_RETVAL DecryptAndDecodeVideo(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                  const DEMUX_PACKET* sample) = 0;

  /**
   * \brief Convert CDM video frame data to Kodi picture format
   * \param codecInstance The instance of VideoCodec to use
   * \param picture The picture object to populate
   * \return status of the conversion
   */
  virtual VIDEOCODEC_RETVAL VideoFrameDataToPicture(kodi::addon::CInstanceVideoCodec* codecInstance,
                                                    VIDEOCODEC_PICTURE* picture) = 0;

  /**
   * \brief Reset the decoder
   */
  virtual void ResetVideo() = 0;

  /**
   * \brief Set the auxillary library path
   * \param libraryPath Filesystem path for the decrypter to locate any needed files such as CDMs
   */
  virtual void SetLibraryPath(std::string_view libraryPath) = 0;

  /**
   * \brief Get the auxillary library path
   * \return The auxillary library path
   */
  virtual std::string_view GetLibraryPath() const = 0;

};
}; // namespace DRM
