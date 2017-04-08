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

class TTML2SRT
{
public:
  TTML2SRT() :m_node(0), m_pos(0), m_timescale(0){};

  bool Parse(const void *buffer, size_t buffer_size, uint64_t timescale);

  bool Prepare(uint64_t &pts, uint32_t &duration);
  const void *GetData() const { return m_SRT.data(); };
  size_t GetDataSize() const { return m_SRT.size(); };

  bool StackSubTitle(const char *s, const char *e, const char *id);
  void StackText();

  struct SUBTITLE
  {
    std::string id;
    uint64_t start, end;
    std::vector<std::string> text;
  };

  // helper
  std::string m_strXMLText;

  static const uint32_t NODE_BODY = 1 << 10;
  static const uint32_t NODE_DIV = 1 << 11;
  static const uint32_t NODE_P = 1 << 12;
  static const uint32_t NODE_SPAN = 1 << 13;

  uint32_t m_node, m_pos;
private:
  std::deque<SUBTITLE> m_subTitles;
  std::string m_SRT, m_lastId;
  uint64_t m_timescale;

};