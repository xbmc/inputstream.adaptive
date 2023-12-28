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
/*!
  * \brief Test if there is a compatible DRM that support the specified key system.
  * \return True if DRM supported, otherwise false.
  */
bool IsKeySystemDRMSupported(std::string_view ks);

namespace FACTORY
{
IDecrypter* GetDecrypter(STREAM_CRYPTO_KEY_SYSTEM keySystem);
}
} // namespace DRM
