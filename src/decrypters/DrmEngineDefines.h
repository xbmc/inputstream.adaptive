/*
 *  Copyright (C) 2025 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "utils/CryptoUtils.h"

#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <vector>

// forwards
class Adaptive_CencSingleSampleDecrypter;
namespace DRM
{
class IDecrypter;
}

namespace DRM
{

constexpr uint16_t HDCP_V_NONE = 0;
constexpr uint16_t HDCP_V_MAX = 9999;

// \brief Decrypter capabilities
struct Capabilities
{
  static const uint32_t SUPPORTS_DECODING = 1;
  static const uint32_t SECURE_PATH = 2;
  static const uint32_t ANNEXB_REQUIRED = 4;
  static const uint32_t HDCP_RESTRICTED = 8;
  static const uint32_t SINGLE_DECRYPT = 16;
  static const uint32_t SECURE_DECODER = 32;
  static const uint32_t INVALID_STATUS = 64;

  uint16_t flags{0};
  bool HasFlag(uint32_t cap) const { return (flags & cap) != 0; }

  /* The following 2 fields are set as followed:
     - If licenseresponse return hdcp information, hdcpversion is 0 and
       hdcplimit either 0 (if hdcp is supported) or given value (if hdcpversion is not supported)
     - if no hdcp information is passed in licenseresponse, we set hdcpversion to the value we support
       manifest / representation have to check if they are allowed to be played.
  */
  uint16_t hdcpVersion{HDCP_V_NONE}; //The HDCP version streams has to be restricted 0,10,20,21,22.....
  int hdcpLimit{0}; // If set (> 0) streams that are greater than the multiplication of "Width x Height" cannot be played.
};

struct Config
{
  // The Key System used to initialize DRM
  std::string keySystem;
  // To enable persistent state CDM behaviour
  bool isPersistentStorage{false};
  // Optional parameters to make the CDM key request (CDM specific parameters)
  std::map<std::string, std::string> optKeyReqParams;

  struct License
  {
    // The license server certificate
    std::vector<uint8_t> serverCert;
    // The license server uri
    std::string serverUri;
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
};

constexpr std::string_view ROBUSTNESS_HW_SECDEC = "HW_SECURE_DECODE";

enum class EngineStatus
{
  NONE,
  DRM_ERROR, // Unsupported DRM, or a problem in the initialization of DRM
  DECRYPTER_ERROR, // A DRM decrypter error
  NOT_SUPPORTED, // The content is not supported by the DRM (e.g. unsupported crypto mode)
};

enum class DRMMediaType
{
  UNKNOWN,
  VIDEO,
  AUDIO
};

struct DRMInfo
{
  std::string keySystem; // Key system, empty if CENC
  CryptoMode cryptoMode = CryptoMode::NONE; // Encryption scheme
  std::string robustness;
  std::vector<uint8_t> initData;
  std::string defaultKid;
  std::string licenseServerUri;
  std::vector<uint8_t> serverCert; // Server certificate
};

struct DRMInstance
{
  std::string keySystem;
  std::shared_ptr<DRM::IDecrypter> drm;

  DRMInstance(std::string_view ks, std::shared_ptr<DRM::IDecrypter> d)
    : keySystem(ks), drm(d)
  {
  }

  bool operator==(const DRMInstance& other) const
  {
    return (keySystem == other.keySystem) && (drm == other.drm);
  }
};

struct DRMSession
{
  std::string id; // DRM session ID
  std::shared_ptr<DRM::IDecrypter> drm; // DRM instance
  std::shared_ptr<Adaptive_CencSingleSampleDecrypter> decrypter; // DRM Decrypter instance
  DRM::Capabilities capabilities;
  std::string kid;
  std::string challenge; // Key request (Challenge) as base64
  DRMMediaType mediaType{DRMMediaType::UNKNOWN};
};

} // namespace DRM
