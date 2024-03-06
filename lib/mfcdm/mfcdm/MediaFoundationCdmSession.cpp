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
        case MF_MEDIAKEY_STATUS_OUTPUT_DOWNSCALED:
            return MFKeyDownScaled;
        case MF_MEDIAKEY_STATUS_INTERNAL_ERROR:
        // Output not allowed is legacy use? Should not happen in normal cases 
        case MF_MEDIAKEY_STATUS_OUTPUT_NOT_ALLOWED:
            return MFKeyError;
        case MF_MEDIAKEY_STATUS_STATUS_PENDING:
            return MFKeyPending;
        case MF_MEDIAKEY_STATUS_RELEASED:
            return MFKeyReleased;
        case MF_MEDIAKEY_STATUS_OUTPUT_RESTRICTED:
            return MFKeyRestricted;
    }
}

std::vector<std::unique_ptr<KeyInfo>> ToCdmKeysInfo(const MFMediaKeyStatus* key_statuses,
                                                    int count)
{
    std::vector<std::unique_ptr<KeyInfo>> keys_info;
    keys_info.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        const auto& key_status = key_statuses[i];
        keys_info.push_back(std::make_unique<KeyInfo>(
            std::vector(key_status.pbKeyId, key_status.pbKeyId + key_status.cbKeyId),
            ToCdmKeyStatus(key_status.eMediaKeyStatus))
        );
    }
    return keys_info;
}

class SessionCallbacks : public winrt::implements<
        SessionCallbacks, IMFContentDecryptionModuleSessionCallbacks>
{
  public:
    using SessionMessage =
        std::function<void(const std::vector<uint8_t>& message, std::string_view destinationUrl)>;
    using KeyChanged =
        std::function<void()>;

    SessionCallbacks(SessionMessage sessionMessage, KeyChanged keyChanged)
      : m_sessionMessage(std::move(sessionMessage)), m_keyChanged(keyChanged){};

    IFACEMETHODIMP KeyMessage(MF_MEDIAKEYSESSION_MESSAGETYPE message_type,
                              const BYTE* message,
                              DWORD message_size,
                              LPCWSTR destination_url) final
    {
        Log(MFCDM::MFLOG_DEBUG, "Message size: %i Destination Url: %S",
            message_size, destination_url);
        m_sessionMessage(std::vector(message, message + message_size),
                         WIDE::ConvertWideToUTF8(destination_url));
        return S_OK;
    }

    IFACEMETHODIMP KeyStatusChanged() final
    {
        Log(MFCDM::MFLOG_DEBUG, "KeyStatusChanged");
        m_keyChanged();
        return S_OK;
    }
private:
    SessionMessage m_sessionMessage;
    KeyChanged m_keyChanged;
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
        std::bind(&MediaFoundationCdmSession::OnSessionMessage, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&MediaFoundationCdmSession::OnKeyChange, this)
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
                                                const std::vector<uint8_t>& initData,
                                                SessionCreatedFunc created)
{
    m_sessionCreated = std::move(created);

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
                                                 std::string_view destinationUrl)
{
    if (!m_client)
        return;

    if (m_sessionCreated)
    {
        m_sessionCreated(GetSessionId());
        m_sessionCreated = SessionCreatedFunc();
    }

    m_client->OnSessionMessage(GetSessionId(), message, destinationUrl);
}

void MediaFoundationCdmSession::OnKeyChange() const
{
    if (!m_client || !mfCdmSession)
        return;
    
    ScopedCoMem<MFMediaKeyStatus> keyStatuses;

    UINT count = 0;
    if (FAILED(mfCdmSession->GetKeyStatuses(&keyStatuses, &count)))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to get key statuses.");
        return;
    }

    m_client->OnKeyChange(GetSessionId(), ToCdmKeysInfo(keyStatuses.get(), count));

    for (UINT i = 0; i < count; ++i)
    {
        const auto& key_status = keyStatuses.get()[i];
        if (key_status.pbKeyId)
            CoTaskMemFree(key_status.pbKeyId);
    }
}

std::string MediaFoundationCdmSession::GetSessionId() const
{
    if (!mfCdmSession)
        return "";

    ScopedCoMem<wchar_t> sessionId;

    if (FAILED(mfCdmSession->GetSessionId(&sessionId)))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to grab MF session's id.");
        return "";
    }

    return WIDE::ConvertWideToUTF8(sessionId.get());
}
