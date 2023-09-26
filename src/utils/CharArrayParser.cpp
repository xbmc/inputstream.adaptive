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

void UTILS::CCharArrayParser::Reset(const uint8_t* data, int limit)
{
  m_data = data;
  m_limit = limit;
  m_position = 0;
}

int UTILS::CCharArrayParser::CharsLeft()
{
  return m_limit - m_position;
}

int UTILS::CCharArrayParser::GetPosition()
{
  return m_position;
}

bool UTILS::CCharArrayParser::SetPosition(int position)
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

bool UTILS::CCharArrayParser::SkipChars(int nChars)
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

std::string UTILS::CCharArrayParser::ReadNextString(int length)
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

bool UTILS::CCharArrayParser::ReadNextArray(int length, std::vector<uint8_t>& data)
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
  return true;
}

bool UTILS::CCharArrayParser::ReadNextLine(std::string& line)
{
  if (!m_data)
  {
    LOG::LogF(LOGERROR, "{} - No data to read");
    return false;
  }
  if (CharsLeft() == 0)
  {
    line.clear();
    return false;
  }

  int lineLimit = m_position;
  while (lineLimit < m_limit && !(m_data[lineLimit] == '\n' || m_data[lineLimit] == '\r'))
  {
    lineLimit++;
  }

  if (lineLimit - m_position >= 3 && m_data[m_position] == '\xEF' &&
      m_data[m_position + 1] == '\xBB' && m_data[m_position + 2] == '\xBF')
  {
    // There's a UTF-8 byte order mark at the start of the line. Discard it.
    m_position += 3;
  }

  line.assign(reinterpret_cast<const char*>(m_data + m_position), lineLimit - m_position);
  m_position = lineLimit;

  if (m_data[m_position] == '\r')
  {
    m_position++;
  }
  if (m_data[m_position] == '\n')
  {
    m_position++;
  }

  return true;
}
