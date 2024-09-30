/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "IDecrypter.h"

#include <memory>

namespace ADP
{
namespace KODI_PROPS
{
struct DrmCfg;
}
}

namespace DRM
{
DRM::Config CreateDRMConfig(std::string_view keySystem, const ADP::KODI_PROPS::DrmCfg& propCfg);

namespace FACTORY
{
std::shared_ptr<DRM::IDecrypter> GetDecrypter(STREAM_CRYPTO_KEY_SYSTEM keySystem);
}
} // namespace DRM
