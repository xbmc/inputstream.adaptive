/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

class MediaFoundationSession {
public:
    ~MediaFoundationSession();

    void Startup();
    void Shutdown();

    [[nodiscard]] bool HasMediaFoundation() const { return hasMediaFoundation; }
private:
    bool hasMediaFoundation = false;
};
