/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CdmFixedBuffer.h"

#include "WVDecrypter.h"

void CdmFixedBuffer::Destroy()
{
  m_host->ReleaseBuffer(m_instance, m_buffer);
  delete this;
}

void CdmFixedBuffer::initialize(void* instance, uint8_t* data, size_t dataSize, void* buffer, CWVDecrypter* host)
{
  m_instance = instance;
  m_data = data;
  m_dataSize = 0;
  m_capacity = dataSize;
  m_buffer = buffer;
  m_host = host;
}
