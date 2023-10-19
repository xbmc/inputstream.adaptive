/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaFoundationSession.h"

#include <unknwn.h>
#include <winrt/base.h>
#include <mfapi.h>

MediaFoundationSession::~MediaFoundationSession() {
    Shutdown();
}

void MediaFoundationSession::Startup() {
    winrt::init_apartment();

    const auto hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    hasMediaFoundation = hr == S_OK;
}

void MediaFoundationSession::Shutdown() {
    if (hasMediaFoundation)
        MFShutdown();
    hasMediaFoundation = false;

    winrt::uninit_apartment();
}
