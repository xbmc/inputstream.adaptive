/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include <cstdint>
#include <string>
#include <vector>

#pragma once

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
  MFKeyExpired = 1,
  MFKeyError = 2
};

class SessionClient
{
public:
  virtual ~SessionClient() = default;

  virtual void OnSessionMessage(std::string_view session,
                                const std::vector<uint8_t>& message,
                                std::string_view destinationUrl) = 0;
};
