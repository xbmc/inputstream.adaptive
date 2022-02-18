/*
 *  Copyright (C) 2019 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "PRProtectionParser.h"

#include "../utils/Base64Utils.h"
#include "../utils/Utils.h"
#include "../utils/log.h"
#include "expat.h"

#include <string.h>

using namespace UTILS;

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
    std::string decKid{BASE64::Decode(parser->m_strXMLText)};

    if (decKid.size() == 16)
    {
      std::string kidUUID{ConvertKIDtoWVKID(decKid)};
      parser->setKID(kidUUID);
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
  {
    wwrmheader += "=";
  }

  std::string xmlData{ BASE64::Decode(wwrmheader) };

  size_t dataStartPoint = xmlData.find('<', 0);
  if (dataStartPoint == std::string::npos)
    return;

  xmlData = xmlData.substr(dataStartPoint);

  XML_Parser xmlParser = XML_ParserCreate("UTF-16");
  if (!xmlParser)
    return;

  XML_SetUserData(xmlParser, (void*)this);
  XML_SetElementHandler(xmlParser, protection_start, protection_end);
  XML_SetCharacterDataHandler(xmlParser, protection_text);

  int done(0);
  if (XML_Parse(xmlParser, xmlData.c_str(), static_cast<int>(xmlData.size()), done) !=
      XML_STATUS_OK)
  {
    LOG::LogF(LOGWARNING, "Failed to parse protection data");
  }

  XML_ParserFree(xmlParser);
}

} // namespace adaptive
