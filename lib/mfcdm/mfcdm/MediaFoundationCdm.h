/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "MediaFoundationSession.h"
#include "MediaFoundationCdmConfig.h"
#include "MediaFoundationCdmTypes.h"

#include <string>
#include <map>
#include <memory>
#include <vector>

class MediaFoundationCdmSession;
class MediaFoundationCdmModule;

class MediaFoundationCdm {
public:
    MediaFoundationCdm();
    ~MediaFoundationCdm();

    bool IsInitialized() const { return m_module != nullptr; }

    bool Initialize(const MediaFoundationCdmConfig& cdmConfig,
                    std::string_view keySystem,
                    std::string_view basePath);

    bool SetServerCertificate(const uint8_t* serverCertificateData,
                              uint32_t serverCertificateDataSize) const;

    bool CreateSessionAndGenerateRequest(SessionType sessionType,
                                         InitDataType initDataType,
                                         const std::vector<uint8_t>& initData,
                                         SessionClient* client);

    void LoadSession(SessionType session_type, const std::string& session_id);
    bool UpdateSession(const std::string& session_id, const std::vector<uint8_t>& response);

private:
    void SetupPMPServer() const;
    MediaFoundationCdmSession* GetSession(const std::string& sessionId) const;
    void OnNewSessionId(int sessionToken, std::string_view sessionId);

    MediaFoundationSession m_session;
    std::unique_ptr<MediaFoundationCdmModule> m_module;

    int m_nextSessionToken = 0;
    std::map<int, std::shared_ptr<MediaFoundationCdmSession>> m_pendingSessions;
    std::map<std::string, std::shared_ptr<MediaFoundationCdmSession>> m_sessions;
};
