/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */
#pragma once

#include "cdm/media/cdm/cdm_adapter.h"

#include <bento4/Ap4.h>
#include <kodi/AddonBase.h>

class ATTR_DLL_LOCAL CdmBuffer : public cdm::Buffer
{
public:
  CdmBuffer(AP4_DataBuffer* buffer) : m_buffer(buffer){};
  virtual ~CdmBuffer(){};

  virtual void Destroy() override{};

  virtual uint32_t Capacity() const override { return m_buffer->GetBufferSize(); };
  virtual uint8_t* Data() override { return (uint8_t*)m_buffer->GetData(); };
  virtual void SetSize(uint32_t size) override { m_buffer->SetDataSize(size); };
  virtual uint32_t Size() const override { return m_buffer->GetDataSize(); };

private:
  AP4_DataBuffer* m_buffer;
};
