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
#include "../helpers.h"
#include <cstring>

bool WebVTT::Parse(uint64_t pts, uint32_t duration, const void *buffer, size_t buffer_size, uint64_t timescale, uint64_t ptsOffset)
{
  m_seekTime = 0;
  m_timescale = timescale;
  m_ptsOffset = ptsOffset;
  if (pts < ptsOffset)
    pts += ptsOffset;

  const char *cbuf(reinterpret_cast<const char*>(buffer)), *cbufe(cbuf + buffer_size);

  if (buffer_size >= 8 && (memcmp(cbuf + 4, "vtte", 4) == 0 || memcmp(cbuf + 4, "vttc", 4) == 0))
  {
    if (memcmp(cbuf + 4, "vtte", 4) == 0)
    {
      if (!m_subTitles.empty() && !~m_subTitles.back().end)
        m_subTitles.back().end = pts;
    }
    else if (memcmp(cbuf + 4, "vttc", 4) == 0)
    {
      if (memcmp(cbuf + 12, "payl", 4) == 0)
        cbuf += 4, buffer_size -= 4;

      std::string text(cbuf + 12, buffer_size - 12);
      if (m_subTitles.empty() || text != m_subTitles.back().text[0])
      {
        m_subTitles.push_back(SUBTITLE(pts));
        m_subTitles.back().text.push_back(text);
      }
    }
  }
  else
  {
    m_subTitles.clear();
    bool webvtt_visited(false);
    bool wait_start(true);
    std::string strText;
    m_pos =  ~0;

    while (cbuf != cbufe)
    {
      const char *next(strchr(cbuf, '\n'));
      if (!next)
        next = cbufe;

      if (webvtt_visited)
      {
        if (*cbuf == '\n' || *cbuf == '\r')
        {
          strText.clear();
          wait_start = true;
        }
        else if (wait_start)
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
            strText.resize(strText.size() - 1);
          replaceAll(strText, "&lrm;", "\xE2\x80\xAA", true);
          replaceAll(strText, "&rlm;", "\xE2\x80\xAB", true);
          if (!strText.empty())
            m_subTitles.back().text.push_back(strText);
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

    if (!~m_pos || m_pos >= m_subTitles.size())
      m_pos = 0;
    else
      ++m_pos;
  }

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

  if (m_pos >= m_subTitles.size() || !~m_subTitles[m_pos].end)
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
