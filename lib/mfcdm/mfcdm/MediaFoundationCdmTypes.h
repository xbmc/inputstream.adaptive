/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum SessionType : uint32_t
{
  MFTemporary = 0,
  MFPersistentLicense = 1
};

enum InitDataType : uint32_t
{
  MFCenc = 0,
  MFKeyIds = 1,
  MFWebM = 2
};

enum KeyStatus : uint32_t
{
  MFKeyUsable = 0,
  MFKeyDownScaled = 1,
  MFKeyPending = 2,
  MFKeyExpired = 3,
  MFKeyReleased = 4,
  MFKeyRestricted = 5, 
  MFKeyError = 6
};

struct KeyInfo
{
  KeyInfo(std::vector<uint8_t> keyId, KeyStatus status)
    : keyId(std::move(keyId)),
      status(status)
  {
    
  }
  std::vector<uint8_t> keyId;
  KeyStatus status;

  bool operator==(KeyInfo const& other) const { return keyId == other.keyId; }
};

class SessionClient
{
public:
  virtual ~SessionClient() = default;

  virtual void OnSessionMessage(std::string_view sessionId,
                                const std::vector<uint8_t>& message,
                                std::string_view destinationUrl) = 0;

  virtual void OnKeyChange(std::string_view sessionId,
                           std::vector<std::unique_ptr<KeyInfo>> keys) = 0;
};
