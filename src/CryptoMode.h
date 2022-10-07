/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */
#pragma once

 // These values must match their respective constant values
 // defined in the Android MediaCodec class
enum class CryptoMode
{
  NONE = 0,
  AES_CTR = 1,
  AES_CBC = 2
};
