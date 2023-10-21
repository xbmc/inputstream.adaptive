/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "MediaFoundationCdmTypes.h"

#include <unknwn.h>
#include <winrt/base.h>

#include <mfapi.h>
#include <mfcontentdecryptionmodule.h>

class MediaFoundationCdmModule;

class MediaFoundationCdmSession {
public:
  MediaFoundationCdmSession(SessionClient* client);

  bool Initialize(MediaFoundationCdmModule* mfCdm, SessionType sessionType);

  bool GenerateRequest(InitDataType initDataType, const std::vector<uint8_t>& initData);
  bool Update(const std::vector<uint8_t>& response);

  std::string GetSessionId() const;

private:

  void OnSessionMessage(const std::vector<uint8_t>& message, std::string_view destinationUrl) const;

  winrt::com_ptr<IMFContentDecryptionModuleSession> mfCdmSession;
  SessionClient* m_client;
};
