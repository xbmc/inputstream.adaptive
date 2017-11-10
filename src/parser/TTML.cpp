/*
*      Copyright (C) 2016-2016 peak3d
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

#include "TTML.h"
#include "expat.h"
#include <cstring>

static void XMLCALL
start(void *data, const char *el, const char **attr)
{
  TTML2SRT *ttml(reinterpret_cast<TTML2SRT*>(data));
  const char *nssep;

  if (el && (nssep = strchr(el, ':')))
    el = nssep + 1;

  if (ttml->m_node & TTML2SRT::NODE_TT)
  {
    if (ttml->m_node & TTML2SRT::NODE_HEAD)
    {
      if (ttml->m_node & TTML2SRT::NODE_STYLING)
      {
        if (strcmp(el, "style") == 0)
        {
          TTML2SRT::STYLE style;
          for (; *attr;)
          {
            if ((nssep = strchr((const char*)*attr, ':')))
              *attr = nssep + 1;
            if (strcmp((const char*)*attr, "id") == 0)
              style.id = (const char*)*(attr + 1);
            else if (strcmp((const char*)*attr, "color") == 0)
              style.color = (const char*)*(attr + 1);
            else if (strcmp((const char*)*attr, "textDecoration") == 0)
              style.underline = strcmp((const char*)*(attr + 1), "underline") == 0 ? 1 : strcmp((const char*)*(attr + 1), "noUnderline") == 0 ? 0 : 0xFF;
            else if (strcmp((const char*)*attr, "fontStyle") == 0)
              style.italic = strcmp((const char*)*(attr + 1), "italic") == 0 ? 1 : strcmp((const char*)*(attr + 1), "normal") == 0 ? 0 : 0xFF;
            else if (strcmp((const char*)*attr, "fontWeight") == 0)
              style.bold = strcmp((const char*)*(attr + 1), "bold") == 0 ? 1 : strcmp((const char*)*(attr + 1), "normal") == 0 ? 0 : 0xFF;
            attr += 2;
          }
          ttml->InsertStyle(style);
        }
      }
      else if (strcmp(el, "styling") == 0)
        ttml->m_node |= TTML2SRT::NODE_STYLING;
    }
    else if (ttml->m_node & TTML2SRT::NODE_BODY)
    {
      if (ttml->m_node & TTML2SRT::NODE_DIV)
      {
        if (ttml->m_node & TTML2SRT::NODE_P)
        {
          if (ttml->m_node & TTML2SRT::NODE_SPAN)
          {
          }
          else if (strcmp(el, "span") == 0)
          {
            const char *style(0);
            for (; *attr && !style; attr += 2)
            {
              if ((nssep = strchr((const char*)*attr, ':')))
                *attr = nssep + 1;
              if (strcmp((const char*)*attr, "style") == 0)
                style = (const char*)*(attr + 1);
            }
            ttml->StackStyle(style);

            ttml->m_node |= TTML2SRT::NODE_SPAN;
          }
          if (strcmp(el, "br") == 0)
            ttml->m_strXMLText += "\n";
        }
        else if (strcmp(el, "p") == 0)
        {
          ttml->m_strXMLText.clear();
          const char *b(0), *e(0), *id("");

          for (; *attr;)
          {
            if ((nssep = strchr((const char*)*attr, ':')))
              *attr = nssep + 1;
            if (strcmp((const char*)*attr, "begin") == 0)
              b = (const char*)*(attr + 1);
            else if (strcmp((const char*)*attr, "end") == 0)
              e = (const char*)*(attr + 1);
            else if (strcmp((const char*)*attr, "id") == 0)
              id = (const char*)*(attr + 1);
            attr += 2;
          }
          if (ttml->StackSubTitle(b, e, id))
            ttml->m_node |= TTML2SRT::NODE_P;
        }
      }
      else if (strcmp(el, "div") == 0)
        ttml->m_node |= TTML2SRT::NODE_DIV;
    }
    else if (strcmp(el, "body") == 0)
    {
      const char *style(0);
      for (; *attr && !style; attr += 2)
      {
        if ((nssep = strchr((const char*)*attr, ':')))
          *attr = nssep + 1;
        if (strcmp((const char*)*attr, "style") == 0)
          style = (const char*)*(attr + 1);
      }
      ttml->StackStyle(style);

      ttml->m_node |= TTML2SRT::NODE_BODY;
    }
    else if (strcmp(el, "head") == 0)
      ttml->m_node |= TTML2SRT::NODE_HEAD;
  }
  else if (strcmp(el, "tt") == 0)
  {
    ttml->m_node |= TTML2SRT::NODE_TT;
    for (; *attr;)
    {
      if ((nssep = strchr((const char*)*attr, ':')))
        *attr = nssep + 1;
      if (strcmp((const char*)*attr, "tickRate") == 0)
        ttml->m_tickRate = atoll((const char*)*(attr + 1));
      attr += 2;
    }
  }
}

static void XMLCALL
text(void *data, const char *s, int len)
{
  TTML2SRT *ttml(reinterpret_cast<TTML2SRT*>(data));
  
  if (ttml->m_node & TTML2SRT::NODE_P)
    ttml->m_strXMLText += std::string(s, len);
}

static void XMLCALL
end(void *data, const char *el)
{
  TTML2SRT *ttml(reinterpret_cast<TTML2SRT*>(data));

  if (ttml->m_node & TTML2SRT::NODE_TT)
  {
    if (ttml->m_node & TTML2SRT::NODE_BODY)
    {
      if (ttml->m_node & TTML2SRT::NODE_DIV)
      {
        if (ttml->m_node & TTML2SRT::NODE_P)
        {
          if (ttml->m_node & TTML2SRT::NODE_SPAN)
          {
            if (strcmp(el, "span") == 0)
            {
              ttml->m_node &= ~TTML2SRT::NODE_SPAN;
              ttml->StackText();
              ttml->UnstackStyle();
            }
          }
          else if (strcmp(el, "p") == 0)
          {
            ttml->m_node &= ~TTML2SRT::NODE_P;
            ttml->StackText();
          }
        }
        else if (strcmp(el, "div") == 0)
          ttml->m_node &= ~TTML2SRT::NODE_DIV;
      }
      else if (strcmp(el, "body") == 0)
      {
        ttml->m_node &= ~TTML2SRT::NODE_BODY;
        ttml->UnstackStyle();
      }
    }
    else if (ttml->m_node & TTML2SRT::NODE_HEAD)
    {
      if (ttml->m_node & TTML2SRT::NODE_STYLING)
      {
        if (strcmp(el, "styling") == 0)
          ttml->m_node &= ~TTML2SRT::NODE_STYLING;
      }
      else if (strcmp(el, "head") == 0)
        ttml->m_node &= ~TTML2SRT::NODE_HEAD;
    }
    else if (strcmp(el, "tt") == 0)
      ttml->m_node &= ~TTML2SRT::NODE_TT;
  }
}

bool TTML2SRT::Parse(const void *buffer, size_t buffer_size, uint64_t timescale, uint64_t ptsOffset)
{
  bool done(true);
  m_node = 0;
  m_pos =  0;
  m_seekTime = 0;
  m_strXMLText.clear();
  m_subTitles.clear();
  m_timescale = timescale;
  m_ptsOffset = ptsOffset;
  m_styles.clear();
  m_styleStack.resize(1);

  XML_Parser parser = XML_ParserCreate(NULL);
  if (!parser)
    return false;
  XML_SetUserData(parser, (void*)this);
  XML_SetElementHandler(parser, start, end);
  XML_SetCharacterDataHandler(parser, text);

  XML_Status retval = XML_Parse(parser, (const char*)buffer, buffer_size, done);
  XML_ParserFree(parser);

  if (retval == XML_STATUS_ERROR)
    return false;
  
  while (m_pos < m_subTitles.size() && m_subTitles[m_pos].id != m_lastId)
    ++m_pos;
  
  if (m_pos == m_subTitles.size())
    m_pos = 0;
  else
    ++m_pos;

  m_lastId.clear();
  
  return true;
}

bool TTML2SRT::Prepare(uint64_t &pts, uint32_t &duration)
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

bool TTML2SRT::TimeSeek(uint64_t seekPos)
{
  m_seekTime = seekPos;
  return true;
}

void TTML2SRT::Reset()
{
  m_subTitles.clear();
  m_pos = 0;
}

uint64_t TTML2SRT::GetTime(const char *tmchar)
{
  uint64_t ret(0);
  if (tmchar[strlen(tmchar) - 1] == 't')
  {
    ret = atoll(tmchar) * m_timescale;
    if (m_tickRate)
      ret /= m_tickRate;
  }
  else
  {
    unsigned int th, tm, ts, tms;
    char del;
    if (sscanf(tmchar, "%u:%u:%u%c%2u", &th, &tm, &ts, &del, &tms) == 5)
    {
      ret = th * 3600 + tm * 60 + ts;
      ret = ret * 1000 + tms * 10;
      ret = (ret * m_timescale) / 1000;
    }
  }
  return ret;
}

bool TTML2SRT::StackSubTitle(const char *s, const char *e, const char *id)
{
  if (!s || !e || !*s || !*e)
    return false;

  m_subTitles.push_back(SUBTITLE());
  SUBTITLE &sub(m_subTitles.back());

  sub.start = GetTime(s);
  sub.end = GetTime(e);

  if (sub.start < m_ptsOffset)
  {
    sub.start += m_ptsOffset;
    sub.end += m_ptsOffset;
  }

  sub.id = *id ? id : s;

  return true;
}

void TTML2SRT::StackText()
{
  if (!m_strXMLText.empty())
  {
    std::string strFmt, strFmtEnd;
    STYLE &curStyle(m_styleStack.back());
    if (!curStyle.color.empty())
    {
      strFmt = "<font color=" + curStyle.color + ">";
      strFmtEnd = "</color>";
    }
    if (curStyle.bold == 1)
    {
      strFmt += "<b>";
      strFmtEnd = "</b>" + strFmtEnd;
    }
    if (curStyle.italic == 1)
    {
      strFmt += "<i>";
      strFmtEnd = "</i>" + strFmtEnd;
    }

    m_subTitles.back().text.push_back(strFmt + m_strXMLText + strFmtEnd);
    m_strXMLText.clear();
  }
}


void TTML2SRT::StackStyle(const char* styleId)
{
  if (styleId)
  {
    const STYLE *sp(0);
    for (auto const &s : m_styles)
    {
      if (s.id == styleId)
      {
        sp = &s;
        break;
      }
    }
    if (sp)
    {
      STYLE s(m_styleStack.back());
      if (!sp->color.empty())
        s.color = sp->color;
      if (sp->bold != 0xFF)
        s.bold = sp->bold;
      if (sp->italic != 0xFF)
        s.italic = sp->italic;
      if (sp->underline != 0xFF)
        s.underline = sp->underline;

      m_styleStack.push_back(s);
      return;
    }
  }
  m_styleStack.push_back(m_styleStack.back());
}

void TTML2SRT::UnstackStyle()
{
  m_styleStack.pop_back();
}
