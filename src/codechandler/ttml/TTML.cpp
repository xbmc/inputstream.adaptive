/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TTML.h"

#include "../../utils/StringUtils.h"
#include "../../utils/XMLUtils.h"
#include "../../utils/log.h"
#include "pugixml.hpp"

#include <cstring>

using namespace pugi;
using namespace UTILS;

bool TTML2SRT::Parse(const void* buffer, size_t bufferSize, uint64_t timescale, uint64_t ptsOffset)
{
  m_currSubPos = 0;
  m_seekTime = 0;
  m_subtitlesList.clear();
  m_timescale = timescale;
  m_ptsOffset = ptsOffset;
  m_styles.clear();
  m_styleStack.resize(1); // Add empty style

  if (!ParseData(buffer, bufferSize))
    return false;

  while (m_currSubPos < m_subtitlesList.size() && m_subtitlesList[m_currSubPos].id != m_lastId)
  {
    ++m_currSubPos;
  }

  if (m_currSubPos == m_subtitlesList.size())
    m_currSubPos = 0;
  else
    ++m_currSubPos;

  m_lastId.clear();

  return true;
}

bool TTML2SRT::TimeSeek(uint64_t seekPos)
{
  m_seekTime = seekPos;
  return true;
}

void TTML2SRT::Reset()
{
  m_subtitlesList.clear();
  m_currSubPos = 0;
}

bool TTML2SRT::Prepare(uint64_t& pts, uint32_t& duration)
{
  if (m_seekTime)
  {
    m_currSubPos = 0;

    while (m_currSubPos < m_subtitlesList.size() && m_subtitlesList[m_currSubPos].end < m_seekTime)
    {
      m_currSubPos++;
    }

    m_seekTime = 0;
  }

  if (m_currSubPos >= m_subtitlesList.size())
    return false;

  SubtitleData& sub = m_subtitlesList[m_currSubPos++];

  pts = sub.start;
  duration = static_cast<uint32_t>(sub.end - sub.start);

  m_preparedSubText = sub.text;
  m_lastId = sub.id;

  return true;
}

bool TTML2SRT::ParseData(const void* buffer, size_t bufferSize)
{
  xml_document doc;
  xml_parse_result parseRes = doc.load_buffer(buffer, bufferSize);

  if (parseRes.status != status_ok)
  {
    LOG::LogF(LOGERROR, "Failed to parse XML data, error code: %i", parseRes.status);
    return false;
  }

  xml_node nodeTT = doc.child("tt");
  if (!nodeTT)
  {
    LOG::LogF(LOGERROR, "Failed to get <tt> tag element.");
    return false;
  }

  m_tickRate = XML::GetAttribUint64(nodeTT, "ttp:tickRate");
  m_frameRate = XML::GetAttribUint64(nodeTT, "ttp:frameRate");

  ParseTagHead(nodeTT);
  ParseTagBody(nodeTT);

  return true;
}

void TTML2SRT::ParseTagHead(pugi::xml_node nodeTT)
{
  xml_node nodeHead = nodeTT.child("head");
  if (!nodeHead)
    return;

  // Parse <styling> tag
  xml_node nodeStyling = nodeHead.child("styling");
  if (nodeStyling)
  {
    // Parse <styling> <style> child tags
    for (xml_node node : nodeStyling.children("style"))
    {
      InsertStyle(ParseStyle(node));
    }
  }
}

void TTML2SRT::ParseTagBody(pugi::xml_node nodeTT)
{
  xml_node nodeBody = nodeTT.child("body");
  if (!nodeBody)
    return;

  StackStyle(XML::GetAttrib(nodeBody, "style"));

  // Parse <body> <div> child tags
  for (xml_node nodeDiv : nodeBody.children("div"))
  {
    // Parse <body> <div> <p> child tags
    for (xml_node nodeP : nodeDiv.children("p"))
    {
      std::string_view id = XML::GetAttrib(nodeP, "xml:id");
      std::string_view beginTime = XML::GetAttrib(nodeP, "begin");
      std::string_view endTime = XML::GetAttrib(nodeP, "end");

      StackStyle(XML::GetAttrib(nodeP, "style"));
      // Parse additional style attributes of node and add them as another style stack
      StackStyle(ParseStyle(nodeP));

      std::string subText;
      // NOTE: subtitle text is contained as children of the <p> tag as PCDATA type
      // so we treat the text as XML nodes
      for (pugi::xml_node subTextNode : nodeP.children())
      {
        if (subTextNode.type() == pugi::node_pcdata)
        {
          // It's a text part
          AppendStyledText(subTextNode.value(), subText);
        }
        else if (subTextNode.type() == pugi::node_element)
        {
          // It's a XML tag
          if (STRING::Compare(subTextNode.name(), "span"))
          {
            StackStyle(XML::GetAttrib(subTextNode, "style"));
            // Parse additional style attributes of node and add them as another style stack
            StackStyle(ParseStyle(subTextNode));

            // Span tag contain parts of text
            AppendStyledText(subTextNode.child_value(), subText);

            UnstackStyle();
            UnstackStyle();
          }
          else if (STRING::Compare(subTextNode.name(), "br"))
          {
            subText += "<br/>";
          }
        }
      }

      UnstackStyle();
      UnstackStyle();
      StackSubtitle(id, beginTime, endTime, subText);
    }
  }
}

void TTML2SRT::AppendStyledText(std::string_view textPart, std::string& subText)
{
  if (!textPart.empty())
  {
    std::string strFmt;
    std::string strFmtEnd;
    Style& curStyle = m_styleStack.back();

    if (!curStyle.color.empty())
    {
      strFmt = "<font color=\"" + curStyle.color + "\">";
      strFmtEnd = "</font>";
    }
    if (curStyle.isFontBold.has_value() && *curStyle.isFontBold)
    {
      strFmt += "<b>";
      strFmtEnd = "</b>" + strFmtEnd;
    }
    if (curStyle.isFontItalic.has_value() && *curStyle.isFontItalic)
    {
      strFmt += "<i>";
      strFmtEnd = "</i>" + strFmtEnd;
    }
    if (curStyle.isFontUnderline.has_value() && *curStyle.isFontUnderline)
    {
      strFmt += "<u>";
      strFmtEnd = "</u>" + strFmtEnd;
    }

    subText += strFmt + textPart.data() + strFmtEnd;
  }
}

TTML2SRT::Style TTML2SRT::ParseStyle(pugi::xml_node node)
{
  Style style;

  style.id = XML::GetAttrib(node, "xml:id");
  style.color = XML::GetAttrib(node, "tts:color");

  std::string styleVal;
  if (XML::QueryAttrib(node, "tts:textDecoration", styleVal))
  {
    if (styleVal == "underline")
      style.isFontUnderline = true;
    else if (styleVal == "noUnderline")
      style.isFontUnderline = false;
  }

  if (XML::QueryAttrib(node, "tts:fontStyle", styleVal))
  {
    if (styleVal == "italic")
      style.isFontItalic = true;
    else if (styleVal == "normal")
      style.isFontItalic = false;
  }

  if (XML::QueryAttrib(node, "tts:fontWeight", styleVal))
  {
    if (styleVal == "bold")
      style.isFontBold = true;
    else if (styleVal == "normal")
      style.isFontBold = false;
  }
  return style;
}

void TTML2SRT::StackStyle(const Style& style)
{
  // Get last style add and merge with the style found
  Style newStyle = m_styleStack.back();

  if (!style.id.empty())
  {
    if (style.id.empty())
      newStyle.id += "_nested";
    else
      newStyle.id = style.id;
  }

  if (!style.color.empty())
    newStyle.color = style.color;

  if (style.isFontBold.has_value())
    newStyle.isFontBold = style.isFontBold;

  if (style.isFontItalic.has_value())
    newStyle.isFontItalic = style.isFontItalic;

  if (style.isFontUnderline.has_value())
    newStyle.isFontUnderline = style.isFontUnderline;

  m_styleStack.emplace_back(newStyle);
}

void TTML2SRT::StackStyle(std::string_view styleId)
{
  if (!styleId.empty())
  {
    auto itStyle = std::find_if(m_styles.begin(), m_styles.end(),
                                [&styleId](const Style& item) { return item.id == styleId; });

    if (itStyle != m_styles.end())
    {
      // Get last style add and merge with the style found
      Style newStyle = m_styleStack.back();

      if (!itStyle->id.empty())
        newStyle.id = itStyle->id;

      if (!itStyle->color.empty())
        newStyle.color = itStyle->color;

      if (itStyle->isFontBold.has_value())
        newStyle.isFontBold = itStyle->isFontBold;

      if (itStyle->isFontItalic.has_value())
        newStyle.isFontItalic = itStyle->isFontItalic;

      if (itStyle->isFontUnderline.has_value())
        newStyle.isFontUnderline = itStyle->isFontUnderline;

      m_styleStack.emplace_back(newStyle);
      return;
    }
  }
  m_styleStack.emplace_back(m_styleStack.back());
}

void TTML2SRT::UnstackStyle()
{
  m_styleStack.pop_back();
}

void TTML2SRT::StackSubtitle(std::string_view id,
                             std::string_view beginTime,
                             std::string_view endTime,
                             std::string_view text)
{
  if (beginTime.empty() || endTime.empty())
    return;

  // Don't stack subtitle if begin and end are equal
  if (beginTime == endTime)
    return;

  SubtitleData newSub;
  newSub.id = id.empty() ? beginTime : id;
  newSub.start = GetTime(beginTime);
  newSub.end = GetTime(endTime);
  newSub.text = text;

  if (newSub.start < m_ptsOffset)
  {
    newSub.start += m_ptsOffset;
    newSub.end += m_ptsOffset;
  }
  else if (m_subtitlesList.size() > 1 && (m_subtitlesList.end() - 2)->start > newSub.start)
  {
    // Fix wrong applied pts_offset
    (m_subtitlesList.end() - 2)->start -= m_ptsOffset;
    (m_subtitlesList.end() - 2)->end -= m_ptsOffset;
  }

  m_subtitlesList.emplace_back(newSub);
}

uint64_t TTML2SRT::GetTime(std::string_view timeExpr)
{
  uint64_t ret{0};
  if (timeExpr.back() == 't')
  {
    ret = STRING::ToUint64(timeExpr) * m_timescale;
    if (m_tickRate > 0)
      ret /= m_tickRate;
  }
  else
  {
    unsigned int th, tm, ts, tf;
    char del, ctf[4];
    if (sscanf(timeExpr.data(), "%u:%u:%u%c%s", &th, &tm, &ts, &del, ctf) == 5)
    {
      sscanf(ctf, "%u", &tf);
      if (strlen(ctf) == 2 && del == '.')
        tf = tf * 10;
      else if (strlen(ctf) == 2 && del == ':')
      {
        if (m_frameRate)
          tf = static_cast<unsigned int>((tf * 1000) / m_frameRate);
        else
          tf = (tf * 1000 / 30);
      }
      ret = th * 3600 + tm * 60 + ts;
      ret = ret * 1000 + tf;
      ret = (ret * m_timescale) / 1000;
    }
  }
  return ret;
}
