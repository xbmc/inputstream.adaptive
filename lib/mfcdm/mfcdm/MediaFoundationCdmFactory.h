/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <string>
#include <memory>
#include <filesystem>

#include <unknwn.h>
#include <winrt/base.h>

#include <mfapi.h>
#include <mfcontentdecryptionmodule.h>

#include <cdm/media/base/cdm_config.h>

class MediaFoundationCdmModule;

class MediaFoundationCdmFactory {
public:
    explicit MediaFoundationCdmFactory(std::string key_system);
    bool Initialize();

    bool IsTypeSupported(const std::string& key_system) const;

    bool CreateMfCdm(const media::CdmConfig& cdm_config,
                     const std::filesystem::path& cdm_path,
                     std::unique_ptr<MediaFoundationCdmModule>& mf_cdm) const;

private:
    std::string m_keySystem;

    winrt::com_ptr<IMFContentDecryptionModuleFactory> m_cdmFactory;
};
