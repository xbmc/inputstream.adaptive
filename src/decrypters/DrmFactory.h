/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "IDecrypter.h"

namespace DRM
{
class CDrmFactory
{
public:
  IDecrypter* GetDecrypter(STREAM_CRYPTO_KEY_SYSTEM keySystem);
};
} // namespace DRM
