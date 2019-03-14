/*
*      Copyright (C) 2018 peak3d
*      http://www.peak3d.de
*
*  This Program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This Program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  <http://www.gnu.org/licenses/>.
*
*/

#include "WebVTT.h"
#include <cstring>

bool WebVTT::Parse(const void *buffer, size_t buffer_size, uint64_t timescale, uint64_t ptsOffset)
{
  bool webvtt_visited(false);
  bool wait_start(true);

  m_pos =  ~0;
  m_seekTime = 0;
  m_subTitles.clear();
  m_timescale = timescale;
  m_ptsOffset = ptsOffset;

  const char *cbuf(reinterpret_cast<const char*>(buffer)), *cbufe(cbuf + buffer_size);
  std::string strText;

  while (cbuf != cbufe)
  {
    const char *next(strchr(cbuf, '\n'));
    if (!next)
      next = cbufe;
  
    if (webvtt_visited)
    {
      if (wait_start)
      {
        unsigned int thb, tmb, tsb, tmsb, the, tme, tse, tmse;
        char delb, dele;

        if (sscanf(cbuf, "%u:%u:%u%c%u --> %u:%u:%u%c%u", &thb, &tmb, &tsb, &delb, &tmsb, &the, &tme, &tse, &dele, &tmse) == 10)
        {
          m_subTitles.push_back(SUBTITLE());
          SUBTITLE &sub(m_subTitles.back());

          sub.start = thb * 3600 + tmb * 60 + tsb;
          sub.start = sub.start * 1000 + tmsb;
          sub.start = (sub.start * m_timescale) / 1000;

          sub.end = the * 3600 + tme * 60 + tse;
          sub.end = sub.end * 1000 + tmse;
          sub.end = (sub.end * m_timescale) / 1000;

          if (sub.start < m_ptsOffset)
          {
            sub.start += m_ptsOffset;
            sub.end += m_ptsOffset;
          }

          if (strText.empty())
            sub.id = std::string(cbuf, 12);
          else
            sub.id = strText;
          
          if (sub.id == m_lastId)
            m_pos = m_subTitles.size() - 1;

          wait_start = false;
        }
        else
        {
          strText = std::string(cbuf, next - cbuf);
          if (!strText.empty() && strText.back() == '\r')
            strText.resize(strText.size() - 1);
        }
      }
      else
      {
        strText = std::string(cbuf, next - cbuf);
        if (!strText.empty() && strText.back() == '\r')
          strText.resize(strText.size() -1);
        if (strText.find("&rlm;", 0, 5) == 0)
        {
          strText.replace(0, 5, "\0xE2\0x80\0x8F");
          strText += "\0xE2\0x80\0x8F";
        }
        else if (strText.find("&lrm;", 0, 5) == 0)
        {
          strText.replace(0, 5, "\0xE2\0x80\0xAA");
          strText += "\0xE2\0x80\0xAC";
        }
        if (!strText.empty())
          m_subTitles.back().text.push_back(strText);
        else
          wait_start = true;
      }
    }
    else
    {
      //TODO: BOM
      while (cbuf < next && *cbuf != 'W')
        ++cbuf;
      if (strncmp(cbuf, "WEBVTT", 6) == 0)
        webvtt_visited = true;
    }
  
    cbuf = next;
    if (cbuf != cbufe)
      ++cbuf;
  }

  if (!~m_pos || m_pos == m_subTitles.size())
    m_pos = 0;
  else
    ++m_pos;

  m_lastId.clear();
  
  return true;
}

bool WebVTT::Prepare(uint64_t &pts, uint32_t &duration)
{
  if (m_seekTime)
  {
    for (m_pos = 0; m_pos < m_subTitles.size() && m_subTitles[m_pos].end < m_seekTime; ++m_pos);
    m_seekTime = 0;
  }

  if (m_pos >= m_subTitles.size())
    return false;

  SUBTITLE &sub(m_subTitles[m_pos++]);
  pts = sub.start;
  duration = static_cast<uint32_t>(sub.end - sub.start);

  m_SRT.clear();
  for (size_t i(0); i < sub.text.size(); ++i)
  {
    if (i) m_SRT += "\r\n";
    m_SRT += sub.text[i];
  }
  m_lastId = sub.id;
  return true;
}

bool WebVTT::TimeSeek(uint64_t seekPos)
{
  m_seekTime = seekPos;
  return true;
}

void WebVTT::Reset()
{
  m_subTitles.clear();
  m_pos = 0;
}
