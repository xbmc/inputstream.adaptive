/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "HelperPr.h"
#include "utils/log.h"
#include "utils/Base64Utils.h"
#include "utils/CharArrayParser.h"
#include "utils/StringUtils.h"
#include "utils/XMLUtils.h"
#include "utils/log.h"
#include "pugixml.hpp"

#include <cstring>

using namespace pugi;
using namespace UTILS;

namespace
{
constexpr uint16_t PLAYREADY_WRM_TAG = 0x0001;

// \brief Convert a PlayReady KID to Widevine KID format
std::vector<uint8_t> ConvertKidtoWv(std::vector<uint8_t> kid)
{
  if (kid.size() != 16)
    return {};

  std::vector<uint8_t> remapped;
  static const size_t remap[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
  // Reordering bytes
  for (size_t i{0}; i < 16; ++i)
  {
    remapped.emplace_back(kid[remap[i]]);
  }
  return remapped;
}
} // unnamed namespace

bool DRM::PRHeaderParser::Parse(std::string_view prHeaderBase64)
{
  return Parse(BASE64::Decode(prHeaderBase64));
}

bool DRM::PRHeaderParser::Parse(const std::vector<uint8_t>& prHeader)
{
  m_KID.clear();
  m_licenseURL.clear();
  m_initData.clear();

  if (prHeader.empty())
    return false;

  m_initData = prHeader;

  // Parse header object data
  CCharArrayParser charParser;
  charParser.Reset(prHeader.data(), prHeader.size());

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

    xml_node nodePROTECTINFO = nodeDATA.child("PROTECTINFO");
    if (nodePROTECTINFO)
    {
      xml_node nodeAlgid = nodePROTECTINFO.child("ALGID");
      if (nodeAlgid)
      {
        auto algid = nodeAlgid.child_value();
        if (algid == "AESCTR")
          m_encryption = EncryptionType::AESCTR;
        else if (algid == "AESCBC")
          m_encryption = EncryptionType::AESCBC;
      }
    }
  }
  else
  {
    // Versions > 4.0 can contains:
    // DATA/PROTECTINFO/KID tag or multiple KID tags on DATA/PROTECTINFO/KIDS
    xml_node nodePROTECTINFO = nodeDATA.child("PROTECTINFO");
    if (nodePROTECTINFO)
    {
      xml_node nodeKID = nodePROTECTINFO.child("KID");
      if (nodeKID)
      {
        kidBase64 = nodeKID.attribute("VALUE").as_string();

        auto algid = nodeKID.attribute("ALGID").as_string();
        if (algid == "AESCTR")
          m_encryption = EncryptionType::AESCTR;
        else if (algid == "AESCBC")
          m_encryption = EncryptionType::AESCBC;
      }
      else
      {
        xml_node nodeKIDS = nodePROTECTINFO.child("KIDS");
        if (nodeKIDS)
        {
          LOG::Log(LOGDEBUG, "Playready header contains %zu KID's.",
                   XML::CountChilds(nodeKIDS, "KID"));
          // We get the first KID
          xml_node nodeKID = nodeKIDS.child("KID");
          if (nodeKID)
          {
            kidBase64 = nodeKID.attribute("VALUE").as_string();

            auto algid = nodeKID.attribute("ALGID").as_string();
            if (algid == "AESCTR")
              m_encryption = EncryptionType::AESCTR;
            else if (algid == "AESCBC")
              m_encryption = EncryptionType::AESCBC;
          }
        }
      }
    }
  }

  if (!kidBase64.empty())
  {
    std::vector<uint8_t> prKid = BASE64::Decode(kidBase64);
    if (prKid.size() == 16)
    {
      m_KID = ConvertKidtoWv(prKid);
    }
    else
      LOG::LogF(LOGWARNING, "KID size %zu instead of 16, KID ignored.", prKid.size());
  }

  xml_node nodeLAURL = nodeDATA.child("LA_URL");
  m_licenseURL = nodeLAURL.child_value();

  return true;
}
