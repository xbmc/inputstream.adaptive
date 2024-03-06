/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaFoundationCdm.h"

#include "MediaFoundationCdmFactory.h"
#include "MediaFoundationCdmModule.h"
#include "MediaFoundationCdmSession.h"
#include "utils/PMPHostWrapper.h"
#include "Log.h"

#include <functional>

MediaFoundationCdm::MediaFoundationCdm() = default;
MediaFoundationCdm::~MediaFoundationCdm() = default;

bool MediaFoundationCdm::Initialize(const MediaFoundationCdmConfig& cdmConfig,
                                    std::string_view keySystem,
                                    std::string_view basePath)
{
    bool ret = true;

    m_session.Startup();

    ret = m_session.HasMediaFoundation();
    if (!ret)
    {
      Log(MFCDM::MFLOG_ERROR, "MF doesn't exist on current system");
      return false;
    }

    const auto m_factory = std::make_unique<MediaFoundationCdmFactory>(keySystem);

    ret = m_factory->Initialize();
    if (!ret)
    {
      Log(MFCDM::MFLOG_ERROR, "MFFactory failed to initialize.");
      return false;
    }

    ret = m_factory->CreateMfCdm(cdmConfig, basePath, m_module);
    if (!ret)
    {
      Log(MFCDM::MFLOG_ERROR, "MFFactory failed to create MF CDM.");
      return false;
    }

    Log(MFCDM::MFLOG_DEBUG, "MF CDM created.");

    SetupPMPServer();
    return true;
}

/*!
 * \brief Setup PMPHostApp
 * IMFContentDecryptionModule->SetPMPHostApp
 * needs to be set if not under UWP or else GenerateChallenge will fail
 * \link https://github.com/microsoft/media-foundation/issues/37#issuecomment-1194534228
 */
void MediaFoundationCdm::SetupPMPServer() const
{
    if (!m_module)
        return;

    const winrt::com_ptr<IMFGetService> spIMFGetService = m_module->As<IMFGetService>();
    winrt::com_ptr<IMFPMPHost> pmpHostApp;

    if(FAILED(spIMFGetService->GetService(
        MF_CONTENTDECRYPTIONMODULE_SERVICE, IID_PPV_ARGS(pmpHostApp.put()))))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to get MF CDM service.");
        return;
    }

    winrt::com_ptr<PMPHostWrapper> spIMFPMPHostApp = winrt::make_self<PMPHostWrapper>(pmpHostApp);
    m_module->SetPMPHostApp(spIMFPMPHostApp.get());
}

bool MediaFoundationCdm::SetServerCertificate(const uint8_t* serverCertificateData,
                                              uint32_t serverCertificateDataSize) const
{
    m_module->SetServerCertificate(serverCertificateData, serverCertificateDataSize);
    return true;
}

bool MediaFoundationCdm::CreateSessionAndGenerateRequest(SessionType sessionType,
                                                         InitDataType initDataType,
                                                         const std::vector<uint8_t>& initData,
                                                         SessionClient* client)
{
    auto session = std::make_shared<MediaFoundationCdmSession>(client);

    if (!session->Initialize(m_module.get(), sessionType))
    {
        return false;
    }

    // when session id is identified, callback is ran.
    // this meant to be able to access UpdateSession()
    // inside MF callback because then session id is known.
    int sessionToken = m_nextSessionToken++;
    m_pendingSessions.emplace(sessionToken, session);
    
    if (!session->GenerateRequest(initDataType, initData, 
        std::bind(&MediaFoundationCdm::OnNewSessionId, this, sessionToken, std::placeholders::_1)))
    {
        return false;
    }
    return true;
}

void MediaFoundationCdm::LoadSession(SessionType sessionType, const std::string& sessionId)
{
   
}

bool MediaFoundationCdm::UpdateSession(const std::string& sessionId,
                                       const std::vector<uint8_t>& response)
{
    if (!m_module)
        return false;

    auto* session = GetSession(sessionId);
    if (!session)
    {
        Log(MFCDM::MFLOG_ERROR, "Couldn't find session in created sessions.");
        return false;
    }

    return session->Update(response);
}

void MediaFoundationCdm::OnNewSessionId(int sessionToken, std::string_view sessionId)
{
    auto itr = m_pendingSessions.find(sessionToken);
    assert(itr != m_pendingSessions.end());

    auto session = std::move(itr->second);
    assert(session);
  
    m_pendingSessions.erase(itr);

    m_sessions.emplace(sessionId, std::move(session));
}

MediaFoundationCdmSession* MediaFoundationCdm::GetSession(const std::string& sessionId) const
{
    auto itr = m_sessions.find(sessionId);
    if (itr == m_sessions.end())
        return nullptr;

    return itr->second.get();
}

