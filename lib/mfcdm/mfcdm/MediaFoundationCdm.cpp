/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaFoundationCdm.h"

#include "Log.h"
#include "MediaFoundationCdmFactory.h"
#include "MediaFoundationCdmModule.h"
#include "MediaFoundationCdmSession.h"

#include "utils/PMPHostWrapper.h"

MediaFoundationCdm::~MediaFoundationCdm() = default;

bool MediaFoundationCdm::Initialize(const std::string &keySystem,
                                    const std::string &basePath,
                                    const media::CdmConfig &cdmConfig,
                                    media::CdmAdapterClient* client)
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

    m_client = client;
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

void MediaFoundationCdm::SetServerCertificate(uint32_t promise_id,
                                              const uint8_t* serverCertificateData,
                                              uint32_t serverCertificateDataSize) const
{
    m_module->SetServerCertificate(serverCertificateData, serverCertificateDataSize);
}

void MediaFoundationCdm::CreateSessionAndGenerateRequest(uint32_t promise_id, cdm::SessionType sessionType,
                                                         cdm::InitDataType initDataType, const uint8_t *init_data,
                                                         uint32_t init_data_size)
{
    auto session = std::make_unique<MediaFoundationCdmSession>();

    if (!session->Initialize(sessionType, m_module.get()))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to create session.");
        return;
    }

    int session_token = next_session_token_++;
    session->GenerateRequest(initDataType, init_data, init_data_size);

    m_cdm_sessions.emplace(session_token, std::move(session));
}

void MediaFoundationCdm::LoadSession(cdm::SessionType session_type,
                                     const std::string &session_id)
{

}

void MediaFoundationCdm::UpdateSession(const std::string &session_id)
{

}




