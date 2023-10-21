/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaFoundationCdmSession.h"

#include "MediaFoundationCdmModule.h"
#include "utils/ScopedCoMem.h"
#include "utils/Wide.h"
#include "Log.h"

#include <functional>
#include <iostream>
#include <ostream>

#include <unknwn.h>
#include <winrt/base.h>
#include <mfapi.h>
#include <mfcontentdecryptionmodule.h>

using namespace UTILS;

MF_MEDIAKEYSESSION_TYPE ToMFSessionType(SessionType session_type)
{
    switch (session_type) 
    {
        case MFPersistentLicense:
            return MF_MEDIAKEYSESSION_TYPE_PERSISTENT_LICENSE;
        case MFTemporary:
        default:
            return MF_MEDIAKEYSESSION_TYPE_TEMPORARY;
    }
}

/*!
 * \link https://www.w3.org/TR/eme-initdata-registry/
 */
LPCWSTR InitDataTypeToString(InitDataType init_data_type)
{
    switch (init_data_type)
    {
        case MFWebM:
            return L"webm";
        case MFCenc:
            return L"cenc";
        case MFKeyIds:
            return L"keyids";
        default:
            return L"unknown";
    }
}

KeyStatus ToCdmKeyStatus(MF_MEDIAKEY_STATUS status)
{
    switch (status)
    {
        case MF_MEDIAKEY_STATUS_USABLE:
            return MFKeyUsable;
        case MF_MEDIAKEY_STATUS_EXPIRED:
            return MFKeyExpired;
        // This is for legacy use and should not happen in normal cases. Map it to
        // internal error in case it happens.
        case MF_MEDIAKEY_STATUS_OUTPUT_NOT_ALLOWED:
            return MFKeyError;
        case MF_MEDIAKEY_STATUS_INTERNAL_ERROR:
            return MFKeyError;
    }
}

class SessionCallbacks : public winrt::implements<
        SessionCallbacks, IMFContentDecryptionModuleSessionCallbacks>
{
  public:
    using SessionMessage =
        std::function<void(const std::vector<uint8_t>& message, std::string_view destinationUrl)>;

    SessionCallbacks(SessionMessage sessionMessage) : m_sessionMessage(std::move(sessionMessage)){};

    IFACEMETHODIMP KeyMessage(MF_MEDIAKEYSESSION_MESSAGETYPE message_type,
                              const BYTE* message,
                              DWORD message_size,
                              LPCWSTR destination_url) final
    {
        Log(MFCDM::MFLOG_DEBUG, "Message size: %d Destination Url: %S",
            message_size, destination_url);
        m_sessionMessage(std::vector(message, message + message_size),
                         ConvertWideToUTF8(destination_url));
        return S_OK;
    }

    IFACEMETHODIMP KeyStatusChanged() final
    {
        std::cout << "KeyStatusChanged" << std::endl;
        return S_OK;
    }
private:
    SessionMessage m_sessionMessage;
};

MediaFoundationCdmSession::MediaFoundationCdmSession(SessionClient* client)
  : m_client(client)
{
    assert(m_client != nullptr);
}

bool MediaFoundationCdmSession::Initialize(MediaFoundationCdmModule* mfCdm,
                                           SessionType sessionType)
{
    const auto session_callbacks = winrt::make<SessionCallbacks>(
      std::bind(&MediaFoundationCdmSession::OnSessionMessage, this, std::placeholders::_1, std::placeholders::_2)
    );
    // |mf_cdm_session_| holds a ref count to |session_callbacks|.
    if (FAILED(mfCdm->CreateSession(ToMFSessionType(sessionType), session_callbacks.get(),
               mfCdmSession.put())))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to create MF CDM session.");
        return false;
    }
    return true;
}

bool MediaFoundationCdmSession::GenerateRequest(InitDataType initDataType,
                                                const std::vector<uint8_t>& initData)
{
    if (FAILED(mfCdmSession->GenerateRequest(InitDataTypeToString(initDataType), initData.data(),
                                             static_cast<DWORD>(initData.size()))))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to generate MF CDM request.");
        return false;
    }
    return true;
}

bool MediaFoundationCdmSession::Update(const std::vector<uint8_t>& response)
{
    if (FAILED(mfCdmSession->Update(response.data(), static_cast<DWORD>(response.size()))))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to update MF CDM with response.");
        return false;
    }
    return true;
}

void MediaFoundationCdmSession::OnSessionMessage(const std::vector<uint8_t>& message,
                                                 std::string_view destinationUrl) const
{
    if (!m_client)
        return;
    m_client->OnSessionMessage(GetSessionId(), message, destinationUrl);
}

std::string MediaFoundationCdmSession::GetSessionId() const
{
    ScopedCoMem<wchar_t> sessionId;

    if (FAILED(mfCdmSession->GetSessionId(&sessionId)))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to grab MF session's id.");
        return "";
    }

    return ConvertWideToUTF8(sessionId.get());
}