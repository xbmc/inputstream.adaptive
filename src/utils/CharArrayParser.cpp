/*
 *  Copyright (C) 2005-2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CharArrayParser.h"

#include "log.h"

#include <cstdint>
#include <cstring>

void UTILS::CCharArrayParser::Reset()
{
  m_limit = 0;
  m_position = 0;
}

void UTILS::CCharArrayParser::Reset(const uint8_t* data, size_t limit)
{
  m_data = data;
  m_limit = limit;
  m_position = 0;
}

size_t UTILS::CCharArrayParser::CharsLeft()
{
  return m_limit - m_position;
}

size_t UTILS::CCharArrayParser::GetPosition()
{
  return m_position;
}

bool UTILS::CCharArrayParser::SetPosition(size_t position)
{
  if (position >= 0 && position <= m_limit)
    m_position = position;
  else
  {
    LOG::LogF(LOGERROR, "{} - Position out of range");
    return false;
  }
  return true;
}

bool UTILS::CCharArrayParser::SkipChars(size_t nChars)
{
  return SetPosition(m_position + nChars);
}

uint8_t UTILS::CCharArrayParser::ReadNextUnsignedChar()
{
  m_position++;
  if (!m_data)
  {
    LOG::LogF(LOGERROR, "{} - No data to read");
    return 0;
  }
  if (m_position > m_limit)
    LOG::LogF(LOGERROR, "{} - Position out of range");
  return static_cast<uint8_t>(m_data[m_position - 1]) & 0xFF;
}

uint16_t UTILS::CCharArrayParser::ReadNextUnsignedShort()
{
  if (!m_data)
  {
    LOG::LogF(LOGERROR, "{} - No data to read");
    return 0;
  }
  m_position += 2;
  if (m_position > m_limit)
    LOG::LogF(LOGERROR, "{} - Position out of range");
  return (static_cast<uint16_t>(m_data[m_position - 2]) & 0xFF) << 8 |
         (static_cast<uint16_t>(m_data[m_position - 1]) & 0xFF);
}

uint16_t UTILS::CCharArrayParser::ReadLENextUnsignedShort()
{
  if (!m_data)
  {
    LOG::LogF(LOGERROR, "{} - No data to read");
    return 0;
  }
  m_position += 2;
  if (m_position > m_limit)
    LOG::LogF(LOGERROR, "{} - Position out of range");
  return (static_cast<uint16_t>(m_data[m_position - 2]) & 0xFF) |
         (static_cast<uint16_t>(m_data[m_position - 1]) & 0xFF) << 8;
}

uint32_t UTILS::CCharArrayParser::ReadNextUnsignedInt24()
{
  if (!m_data)
  {
    LOG::LogF(LOGERROR, "{} - No data to read");
    return 0;
  }
  m_position += 3;
  if (m_position > m_limit)
    LOG::LogF(LOGERROR, "{} - Position out of range");
  return (static_cast<uint32_t>(m_data[m_position - 3]) & 0xFF) << 16 |
         (static_cast<uint32_t>(m_data[m_position - 2]) & 0xFF) << 8 |
         (static_cast<uint32_t>(m_data[m_position - 1]) & 0xFF);
}

uint32_t UTILS::CCharArrayParser::ReadNextUnsignedInt()
{
  if (!m_data)
  {
    LOG::LogF(LOGERROR, "{} - No data to read");
    return 0;
  }
  m_position += 4;
  if (m_position > m_limit)
    LOG::LogF(LOGERROR, "{} - Position out of range");
  return (static_cast<uint32_t>(m_data[m_position - 4]) & 0xFF) << 24 |
         (static_cast<uint32_t>(m_data[m_position - 3]) & 0xFF) << 16 |
         (static_cast<uint32_t>(m_data[m_position - 2]) & 0xFF) << 8 |
         (static_cast<uint32_t>(m_data[m_position - 1]) & 0xFF);
}

uint32_t UTILS::CCharArrayParser::ReadNextLEUnsignedInt()
{
  if (!m_data)
  {
    LOG::LogF(LOGERROR, "{} - No data to read");
    return 0;
  }
  m_position += 4;
  if (m_position > m_limit)
    LOG::LogF(LOGERROR, "{} - Position out of range");
  return (static_cast<uint32_t>(m_data[m_position - 4]) & 0xFF) |
         (static_cast<uint32_t>(m_data[m_position - 3]) & 0xFF) << 8 |
         (static_cast<uint32_t>(m_data[m_position - 2]) & 0xFF) << 16 |
         (static_cast<uint32_t>(m_data[m_position - 1]) & 0xFF) << 24;
}

uint64_t UTILS::CCharArrayParser::ReadNextUnsignedInt64()
{
  if (!m_data)
  {
    LOG::LogF(LOGERROR, "{} - No data to read");
    return 0;
  }
  m_position += 8;
  if (m_position > m_limit)
    LOG::LogF(LOGERROR, "{} - Position out of range");
  return (static_cast<uint64_t>(m_data[m_position - 8]) & 0xFF) << 56 |
         (static_cast<uint64_t>(m_data[m_position - 7]) & 0xFF) << 48 |
         (static_cast<uint64_t>(m_data[m_position - 6]) & 0xFF) << 40 |
         (static_cast<uint64_t>(m_data[m_position - 5]) & 0xFF) << 32 |
         (static_cast<uint64_t>(m_data[m_position - 4]) & 0xFF) << 24 |
         (static_cast<uint64_t>(m_data[m_position - 3]) & 0xFF) << 16 |
         (static_cast<uint64_t>(m_data[m_position - 2]) & 0xFF) << 8 |
         (static_cast<uint64_t>(m_data[m_position - 1]) & 0xFF);
}

std::string UTILS::CCharArrayParser::ReadNextString(size_t length)
{
  if (!m_data)
  {
    LOG::LogF(LOGERROR, "{} - No data to read");
    return "";
  }
  std::string str(reinterpret_cast<const char*>(m_data + m_position), length);
  m_position += length;
  if (m_position > m_limit)
    LOG::LogF(LOGERROR, "{} - Position out of range");
  return str;
}

bool UTILS::CCharArrayParser::ReadNextArray(size_t length, std::vector<uint8_t>& data)
{
  if (!m_data)
  {
    LOG::LogF(LOGERROR, "{} - No data to read");
    return false;
  }
  if (m_position + length > m_limit)
  {
    LOG::LogF(LOGERROR, "{} - Position out of range");
    return false;
  }
  data.insert(data.end(), m_data + m_position, m_data + m_position + length);
  m_position += length;
  return true;
}
