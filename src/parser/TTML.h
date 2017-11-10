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

#pragma once

#include <inttypes.h>
#include <deque>
#include <vector>
#include <string>

class TTML2SRT
{
public:
  TTML2SRT() :m_node(0), m_pos(0), m_tickRate(0), m_timescale(0), m_ptsOffset(0) { m_styleStack.push_back(STYLE()); };

  bool Parse(const void *buffer, size_t buffer_size, uint64_t timescale, uint64_t ptsOffset);

  bool Prepare(uint64_t &pts, uint32_t &duration);
  bool TimeSeek(uint64_t seekPos);
  void Reset();

  const void *GetData() const { return m_SRT.data(); };
  size_t GetDataSize() const { return m_SRT.size(); };

  bool StackSubTitle(const char *s, const char *e, const char *id);
  void StackText();

  void StackStyle(const char* styleId);
  void UnstackStyle();

  struct STYLE
  {
    STYLE() :italic(0xFF), bold(0xFF), underline(0xFF) {};
    std::string id;
    std::string color;

    uint8_t italic;
    uint8_t bold;
    uint8_t underline;
    uint8_t dummy;
  };
  void InsertStyle(const STYLE &style) { m_styles.push_back(style); };

  struct SUBTITLE
  {
    std::string id;
    uint64_t start, end;
    std::vector<std::string> text;
  };

  // helper
  std::string m_strXMLText;

  static const uint32_t NODE_TT = 1 << 0;
  static const uint32_t NODE_HEAD = 1 << 1;
  static const uint32_t NODE_STYLING = 1 << 2;
  static const uint32_t NODE_BODY = 1 << 10;
  static const uint32_t NODE_DIV = 1 << 11;
  static const uint32_t NODE_P = 1 << 12;
  static const uint32_t NODE_SPAN = 1 << 13;

  uint32_t m_node, m_pos;
  uint64_t m_tickRate;
private:
  uint64_t GetTime(const char * tm);

  std::deque<SUBTITLE> m_subTitles;
  std::vector<STYLE> m_styles, m_styleStack;

  std::string m_SRT, m_lastId;
  uint64_t m_timescale, m_ptsOffset;
  uint64_t m_seekTime;
};
