/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaFoundationCdmSession.h"

#include "MediaFoundationCdmModule.h"
#include "Log.h"

#include <iostream>
#include <ostream>

#include <unknwn.h>
#include <winrt/base.h>
#include <mfapi.h>
#include <mfcontentdecryptionmodule.h>

MF_MEDIAKEYSESSION_TYPE ToMFSessionType(cdm::SessionType session_type)
{
    switch (session_type) 
    {
        case cdm::SessionType::kPersistentLicense:
            return MF_MEDIAKEYSESSION_TYPE_PERSISTENT_LICENSE;
        case cdm::SessionType::kTemporary:
        default:
            return MF_MEDIAKEYSESSION_TYPE_TEMPORARY;
    }
}

/*!
 * \link https://www.w3.org/TR/eme-initdata-registry/
 */
LPCWSTR InitDataTypeToString(cdm::InitDataType init_data_type)
{
    switch (init_data_type)
    {
        case cdm::InitDataType::kWebM:
            return L"webm";
        case cdm::InitDataType::kCenc:
            return L"cenc";
        case cdm::InitDataType::kKeyIds:
            return L"keyids";
        default:
            return L"unknown";
    }
}

class SessionCallbacks : public winrt::implements<
        SessionCallbacks, IMFContentDecryptionModuleSessionCallbacks>
{
public:
    SessionCallbacks() = default;

    IFACEMETHODIMP KeyMessage(MF_MEDIAKEYSESSION_MESSAGETYPE message_type,
                              const BYTE* message,
                              DWORD message_size,
                              LPCWSTR destination_url) final
    {
        std::wstring messageStr = std::wstring(reinterpret_cast<const wchar_t*>(message), message_size);
        //std::wcout << "KeyMessage: " << messageStr << std::endl;
        Log(MFCDM::MFLOG_DEBUG, "Destination Url %S", destination_url);
        return S_OK;
    }

    IFACEMETHODIMP KeyStatusChanged() final
    {
        std::cout << "KeyStatusChanged" << std::endl;
        return S_OK;
    }
};

bool MediaFoundationCdmSession::Initialize(cdm::SessionType session_type,
                                           MediaFoundationCdmModule* mf_cdm)
{
    const winrt::com_ptr<IMFContentDecryptionModuleSessionCallbacks>
        session_callbacks = winrt::make<SessionCallbacks>();
    // |mf_cdm_session_| holds a ref count to |session_callbacks|.
    if (FAILED(mf_cdm->CreateSession(ToMFSessionType(session_type), session_callbacks.get(),
               mfCdmSession.put())))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to create MF CDM session.");
        return false;
    }
    return true;
}

void MediaFoundationCdmSession::GenerateRequest(cdm::InitDataType init_data_type, const uint8_t *init_data,
                                                uint32_t init_data_size)
{
    if (FAILED(mfCdmSession->GenerateRequest(InitDataTypeToString(init_data_type), init_data,
                                             init_data_size)))
    {
        Log(MFCDM::MFLOG_ERROR, "Failed to generate MF CDM request.");
        return;
    }
    
}
