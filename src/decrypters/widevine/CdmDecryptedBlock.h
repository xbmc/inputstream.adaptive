/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cdm/media/cdm/cdm_adapter.h"

class ATTR_DLL_LOCAL CdmDecryptedBlock : public cdm::DecryptedBlock
{
public:
  CdmDecryptedBlock() : m_buffer(0), m_timeStamp(0){};
  virtual ~CdmDecryptedBlock(){};

  virtual void SetDecryptedBuffer(cdm::Buffer* buffer) override { m_buffer = buffer; };
  virtual cdm::Buffer* DecryptedBuffer() override { return m_buffer; };

  virtual void SetTimestamp(int64_t timestamp) override { m_timeStamp = timestamp; };
  virtual int64_t Timestamp() const override { return m_timeStamp; };

private:
  cdm::Buffer* m_buffer;
  int64_t m_timeStamp;
};
