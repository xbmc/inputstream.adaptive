/*
 *  Copyright (C) 2005-2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "../src/SSD_dll.h"

#ifdef _WIN32
#define MODULE_API __declspec(dllexport)
#else
#define MODULE_API
#endif

SSD::SSD_DECRYPTER MODULE_API *CreateDecryptorInstance(class SSD::SSD_HOST *h, uint32_t host_version);

void DeleteDecryptorInstance(SSD::SSD_DECRYPTER *d);
