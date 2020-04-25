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

#pragma once

#include <inttypes.h>
#include <deque>
#include <vector>
#include <string>

class WebVTT
{
public:
  WebVTT() :m_pos(0), m_tickRate(0), m_timescale(0), m_ptsOffset(0) { };

  bool Parse(uint64_t pts, uint32_t duration, const void *buffer, size_t buffer_size, uint64_t timescale, uint64_t ptsOffset);

  bool Prepare(uint64_t &pts, uint32_t &duration);
  bool TimeSeek(uint64_t seekPos);
  void Reset();

  const void *GetData() const { return m_SRT.data(); };
  size_t GetDataSize() const { return m_SRT.size(); };

  struct SUBTITLE
  {
    SUBTITLE() = default;
    SUBTITLE(uint64_t start)
      : start(start) {};
    std::string id;
    uint64_t start = 0, end = ~0;
    std::vector<std::string> text;
  };

  uint32_t m_pos;
  uint64_t m_tickRate;
private:
  std::deque<SUBTITLE> m_subTitles;

  std::string m_SRT, m_lastId;
  uint64_t m_timescale, m_ptsOffset;
  uint64_t m_seekTime = 0;
};
