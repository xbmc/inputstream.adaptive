/*
*      Copyright (C) 2019 peak3d
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

#include "PRProtectionParser.h"
#include "expat.h"
#include "../helpers.h"
#include <string.h>

namespace adaptive
{

/*----------------------------------------------------------------------
|   expat protection start
+---------------------------------------------------------------------*/
static void XMLCALL
  protection_start(void *data, const char *el, const char **attr)
{
  PRProtectionParser *parser(reinterpret_cast<PRProtectionParser*>(data));
  parser->m_strXMLText.clear();
}

/*----------------------------------------------------------------------
|   expat protection text
+---------------------------------------------------------------------*/
static void XMLCALL
  protection_text(void *data, const char *s, int len)
{
  PRProtectionParser *parser(reinterpret_cast<PRProtectionParser*>(data));
  parser->m_strXMLText += std::string(s, len);
}

/*----------------------------------------------------------------------
|   expat protection end
+---------------------------------------------------------------------*/
static void XMLCALL
  protection_end(void *data, const char *el)
{
  PRProtectionParser *parser(reinterpret_cast<PRProtectionParser*>(data));
  if (strcmp(el, "KID") == 0)
  {
    uint8_t buffer[32];
    unsigned int buffer_size(32);
    b64_decode(parser->m_strXMLText.data(), parser->m_strXMLText.size(), buffer, buffer_size);

    if (buffer_size == 16)
    {
      char kid[17]; kid[16] = 0;
      prkid2wvkid(reinterpret_cast<const char *>(buffer), kid);
      parser->setKID(std::string(kid, 16));
    }
  }
  else if (strcmp(el, "LA_URL") == 0)
  {
    parser->setLicenseURL(parser->m_strXMLText);
  }
}

PRProtectionParser::PRProtectionParser(std::string wwrmheader)
{
  if (wwrmheader.empty())
    return;

  //(p)repair the content
  std::string::size_type pos = 0;
  while ((pos = wwrmheader.find('\n', 0)) != std::string::npos)
    wwrmheader.erase(pos, 1);

  while (wwrmheader.size() & 3)
    wwrmheader += "=";

  unsigned int xml_size = wwrmheader.size();
  uint8_t *buffer = (uint8_t*)malloc(xml_size), *xml_start(buffer);

  if (!b64_decode(wwrmheader.c_str(), xml_size, buffer, xml_size))
  {
    free(buffer);
    return;
  }

  m_strPSSH = std::string(reinterpret_cast<char*>(buffer), xml_size);

  while (xml_size && *xml_start != '<')
  {
    xml_start++;
    xml_size--;
  }

  XML_Parser pp = XML_ParserCreate("UTF-16");
  if (!pp)
  {
    free(buffer);
    return;
  }

  XML_SetUserData(pp, (void*)this);
  XML_SetElementHandler(pp, protection_start, protection_end);
  XML_SetCharacterDataHandler(pp, protection_text);

  bool done(false);
  XML_Parse(pp, (const char*)(xml_start), xml_size, done);

  XML_ParserFree(pp);
  free(buffer);
}


}//namespace