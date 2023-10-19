/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "MediaFoundationSession.h"

#include <string>
#include <map>
#include <memory>

#include "cdm/media/base/cdm_config.h"
#include "cdm/media/cdm/api/content_decryption_module.h"
#include "cdm/media/cdm/cdm_adapter.h"

class MediaFoundationCdmSession;
class MediaFoundationCdmFactory;
class MediaFoundationCdmModule;

class MediaFoundationCdm {
public:
    ~MediaFoundationCdm();

    bool IsInitialized() const { return m_module != nullptr; }

    bool Initialize(const std::string& keySystem,
                    const std::string &basePath,
                    const media::CdmConfig &cdmConfig,
                    media::CdmAdapterClient* client);

    void SetServerCertificate(uint32_t promise_id,
                              const uint8_t* serverCertificateData,
                              uint32_t serverCertificateDataSize) const;

    void CreateSessionAndGenerateRequest(uint32_t promise_id,
                                         cdm::SessionType sessionType,
                                         cdm::InitDataType initDataType,
                                         const uint8_t* init_data,
                                         uint32_t init_data_size);

    void LoadSession(cdm::SessionType session_type, const std::string& session_id);

    void UpdateSession(const std::string& session_id);
private:
    void SetupPMPServer() const;

    MediaFoundationSession m_session;

    std::unique_ptr<MediaFoundationCdmModule> m_module{nullptr};

    int next_session_token_{0};
    std::map<int, std::unique_ptr<MediaFoundationCdmSession>> m_cdm_sessions;

    media::CdmAdapterClient* m_client{nullptr};
};
