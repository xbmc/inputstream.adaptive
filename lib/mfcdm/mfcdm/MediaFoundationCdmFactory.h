/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "MediaFoundationCdmConfig.h"

#include <string>
#include <memory>
#include <filesystem>

#include <unknwn.h>
#include <winrt/base.h>

#include <mfapi.h>
#include <mfcontentdecryptionmodule.h>

class MediaFoundationCdmModule;

class MediaFoundationCdmFactory {
public:
    explicit MediaFoundationCdmFactory(std::string_view keySystem);
    bool Initialize();

    bool IsTypeSupported(std::string_view keySystem) const;

    bool CreateMfCdm(const MediaFoundationCdmConfig& cdmConfig,
                     const std::filesystem::path& cdmPath,
                     std::unique_ptr<MediaFoundationCdmModule>& mfCdm) const;

private:
    std::string m_keySystem;

    winrt::com_ptr<IMFContentDecryptionModuleFactory> m_cdmFactory;
};
