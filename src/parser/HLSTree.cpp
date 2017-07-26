/*
*      Copyright (C) 2017 peak3d
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

#include "HLSTree.h"
#include <map>

using namespace adaptive;

static void parseLine(const std::string &line, size_t offset, std::map<std::string, std::string> &map)
{
  size_t value, end;
  map.clear();
  while (offset < line.size() && (value = line.find('=', offset)) != std::string::npos)
  {
    end = value;
    uint8_t inValue(0);
    while (++end < line.size() && ((inValue & 1) || line[end] != ','))
      if (line[end] == '\"') ++inValue;
    map[line.substr(offset, value - offset)] = line.substr(value + (inValue ? 2: 1), end - value - (inValue ? 3 : 1));
    offset = end + 1;
  }
}


bool HLSTree::open(const char *url)
{
  if (download(url, manifest_headers_))
  {
    std::string line;
    bool startCodeFound = false;

    Representation rep;
    std::map<std::string, std::string> map;

    while (std::getline(m_stream, line))
    {
      if (!startCodeFound && line.compare(0, 7, "#EXTM3U") == 0)
        startCodeFound = true;
      else if (!startCodeFound)
        continue;

      if (line.compare(0, 13, "#EXT-X-MEDIA:") == 0)
      {
        //TODO
      }
      else if (line.compare(0, 18, "#EXT-X-STREAM-INF:") == 0)
      {
        //#EXT-X-STREAM-INF:BANDWIDTH=263851,CODECS="mp4a.40.2, avc1.4d400d",RESOLUTION=416x234,AUDIO="bipbop_audio",SUBTITLES="subs"
        parseLine(line, 18, map);
      }
      else if (line.compare(0, 1, "#") != 0)
      {
      }
    }
    return true;
  }
  return false;
}

bool HLSTree::write_data(void *buffer, size_t buffer_size)
{
  m_stream.write(static_cast<const char*>(buffer), buffer_size);
  return true;
}