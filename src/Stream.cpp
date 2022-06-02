/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Stream.h"

using namespace SESSION;

void CStream::Disable()
{
  if (m_isEnabled)
  {
    m_adStream.stop();
    Reset();
    m_isEnabled = false;
    m_isEncrypted = false;
  }
}

void CStream::Reset()
{
  if (m_isEnabled)
  {
    m_streamReader.reset();
    m_streamFile.reset();
    m_adByteStream.reset();
    m_mainId = 0;
  }
}
