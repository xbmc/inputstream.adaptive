/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cinttypes>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef INPUTSTREAM_TEST_BUILD
#include "../test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

// Forward
namespace pugi
{
class xml_node;
}

class ATTR_DLL_LOCAL TTML2SRT
{
public:
  TTML2SRT() {}

  bool Parse(const void* buffer, size_t bufferSize, uint64_t timescale, uint64_t ptsOffset);

  bool TimeSeek(uint64_t seekPos);

  void Reset();

  bool Prepare(uint64_t& pts, uint32_t& duration);
  const char* GetPreparedData() const { return m_preparedSubText.c_str(); }
  size_t GetPreparedDataSize() const { return m_preparedSubText.size(); }

private:
  bool ParseData(const void* buffer, size_t bufferSize);
  void ParseTagHead(pugi::xml_node nodeHead);
  void ParseTagBody(pugi::xml_node nodeTT);

  struct Style
  {
    std::string id;
    std::string color;
    std::optional<bool> isFontItalic;
    std::optional<bool> isFontBold;
    std::optional<bool> isFontUnderline;
  };

  void AppendStyledText(std::string_view textPart, std::string& subText);

  Style ParseStyle(pugi::xml_node node);

  void InsertStyle(const Style& style) { m_styles.emplace_back(style); }

  void StackStyle(const Style& style);
  void StackStyle(std::string_view styleId);
  void UnstackStyle();

  void StackSubtitle(std::string_view id,
                     std::string_view beginTime,
                     std::string_view endTime,
                     std::string_view text);

  uint64_t GetTime(std::string_view timeExpr);

  struct SubtitleData
  {
    std::string id;
    uint64_t start{0};
    uint64_t end{0};
    std::string text;
  };

  size_t m_currSubPos{0};
  std::deque<SubtitleData> m_subtitlesList;

  std::vector<Style> m_styles;
  std::vector<Style> m_styleStack;

  std::string m_preparedSubText;
  std::string m_lastId;

  uint64_t m_timescale{0};
  uint64_t m_ptsOffset{0};
  uint64_t m_seekTime{0};
  uint64_t m_tickRate{0};
  uint64_t m_frameRate{0};
};
