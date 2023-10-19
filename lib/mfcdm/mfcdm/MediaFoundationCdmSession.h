/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <unknwn.h>
#include <winrt/base.h>

#include <mfapi.h>
#include <mfcontentdecryptionmodule.h>

#include <cdm/media/cdm/api/content_decryption_module.h>

class MediaFoundationCdmModule;

class MediaFoundationCdmSession {
public:
  bool Initialize(cdm::SessionType session_type, MediaFoundationCdmModule* mf_cdm);
  void GenerateRequest(cdm::InitDataType init_data_type,
                       const uint8_t* init_data, uint32_t init_data_size);
private:
    winrt::com_ptr<IMFContentDecryptionModuleSession> mfCdmSession;
};
