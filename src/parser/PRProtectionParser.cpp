/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "PRProtectionParser.h"

#include "utils/Base64Utils.h"
#include "utils/CharArrayParser.h"
#include "utils/StringUtils.h"
#include "utils/Utils.h"
#include "utils/XMLUtils.h"
#include "utils/log.h"
#include "pugixml.hpp"

using namespace pugi;
using namespace UTILS;

namespace
{
constexpr uint16_t PLAYREADY_WRM_TAG = 0x0001;
} // unnamed namespace

bool adaptive::PRProtectionParser::ParseHeader(std::string_view prHeader)
{
  m_KID.clear();
  m_licenseURL.clear();
  m_PSSH.clear();

  if (prHeader.empty())
    return false;

  std::vector<uint8_t> headerData = BASE64::DecodeStrToUint8(prHeader.data());
  m_PSSH = headerData;

  // Parse header object data
  CCharArrayParser charParser;
  charParser.Reset(headerData.data(), static_cast<int>(headerData.size()));

  if (!charParser.SkipChars(4))
  {
    LOG::LogF(LOGERROR, "Failed parse PlayReady object, no \"length\" field");
    return false;
  }

  if (charParser.CharsLeft() < 2)
  {
    LOG::LogF(LOGERROR, "Failed parse PlayReady object, no number of object records");
    return false;
  }
  uint16_t numRecords = charParser.ReadLENextUnsignedShort();

  std::string xmlData;

  for (uint16_t i = 0; i < numRecords; i++)
  {
    if (charParser.CharsLeft() < 2)
    {
      LOG::LogF(LOGERROR, "Failed parse PlayReady object record %u, cannot read record type", i);
      return false;
    }
    uint16_t recordType = charParser.ReadLENextUnsignedShort();

    if (charParser.CharsLeft() < 2)
    {
      LOG::LogF(LOGERROR, "Failed parse PlayReady object record %u, cannot read record size", i);
      return false;
    }
    uint16_t recordSize = charParser.ReadLENextUnsignedShort();

    if (charParser.CharsLeft() < recordSize)
    {
      LOG::LogF(LOGERROR, "Failed parse PlayReady object record %u, cannot read WRM header", i);
      return false;
    }
    if ((recordType & PLAYREADY_WRM_TAG) == PLAYREADY_WRM_TAG)
    {
      xmlData = charParser.ReadNextString(recordSize);
      break;
    }
    else
      charParser.SkipChars(recordSize);
  }

  // Parse XML header data
  xml_document doc;
  xml_parse_result parseRes = doc.load_buffer(xmlData.c_str(), xmlData.size());
  if (parseRes.status != status_ok)
  {
    LOG::LogF(LOGERROR, "Failed to parse the Playready header, error code: %i", parseRes.status);
    return false;
  }

  xml_node nodeWRM = doc.child("WRMHEADER");
  if (!nodeWRM)
  {
    LOG::LogF(LOGERROR, "<WRMHEADER> node not found.");
    return false;
  }

  std::string_view ver = XML::GetAttrib(nodeWRM, "version");
  LOG::Log(LOGDEBUG, "Parsing Playready header version %s", ver.data());

  xml_node nodeDATA = nodeWRM.child("DATA");
  if (!nodeDATA)
  {
    LOG::LogF(LOGERROR, "<DATA> node not found.");
    return false;
  }

  std::string kidBase64;

  if (STRING::StartsWith(ver, "4.0"))
  {
    // Version 4.0 have KID within DATA tag
    xml_node nodeKID = nodeDATA.child("KID");
    kidBase64 = nodeKID.child_value();
  }
  else
  {
    // Versions > 4.0 can contains one or more optionals KID's within DATA/PROTECTINFO/KIDS tag
    xml_node nodePROTECTINFO = nodeDATA.child("PROTECTINFO");
    if (nodePROTECTINFO)
    {
      xml_node nodeKIDS = nodePROTECTINFO.child("KIDS");
      if (nodeKIDS)
      {
        LOG::Log(LOGDEBUG, "Playready header contains %zu KID's.",
                 XML::CountChilds(nodeKIDS, "KID"));
        // We get the first KID
        xml_node nodeKID = nodeKIDS.child("KID");
        kidBase64 = nodeKID.child_value();
      }
    }
  }

  if (!kidBase64.empty())
  {
    std::vector<uint8_t> kid = BASE64::DecodeStrToUint8(kidBase64);
    if (kid.size() == 16)
    {
      m_KID = ConvertKIDtoWVKID(kid);
    }
    else
      LOG::LogF(LOGWARNING, "KID size %zu instead of 16, KID ignored.", kid.size());
  }

  xml_node nodeLAURL = nodeDATA.child("LA_URL");
  m_licenseURL = nodeLAURL.child_value();

  return true;
}
