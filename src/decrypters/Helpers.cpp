/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Helpers.h"

#include "utils/Base64Utils.h"
#include "utils/DigestMD5Utils.h"
#include "utils/JsonUtils.h"
#include "utils/StringUtils.h"
#include "utils/UrlUtils.h"
#include "utils/XMLUtils.h"
#include "utils/log.h"

#include <pugixml.hpp>

using namespace pugi;
using namespace UTILS;

namespace
{

// Supported wrappers
enum class Wrapper
{
  AUTO, // Try auto-detect wrappers
  NONE, // Implicit for raw binary data
  BASE64,
  JSON,
  XML
};

// \brief Translate a wrapper string in to relative vector of enum values.
//        e.g. "json+base64" --> JSON, BASE64
std::vector<Wrapper> TranslateWrapper(std::string_view wrapper)
{
  const std::vector<std::string> wrappers = STRING::SplitToVec(wrapper, '+');

  if (wrappers.empty())
    return {};

  std::vector<Wrapper> result;
  // Here we have to keep the order because
  // defines the order in which data will be unwrapped
  for (const std::string& wrapper : wrappers)
  {
    if (wrapper == "auto")
      result.emplace_back(Wrapper::AUTO);
    else if (wrapper == "none")
      result.emplace_back(Wrapper::NONE);
    else if (wrapper == "base64")
      result.emplace_back(Wrapper::BASE64);
    else if (wrapper == "json")
      result.emplace_back(Wrapper::JSON);
    else if (wrapper == "xml")
      result.emplace_back(Wrapper::XML);
    else
    {
      LOG::LogF(LOGERROR, "Cannot translate license wrapper, unknown type \"%s\"", wrapper.c_str());
      return {Wrapper::AUTO};
    }
  }
  return result;
}

} // unnamed namespace

bool DRM::IsKeySystemSupported(std::string_view keySystem)
{
  return keySystem == DRM::KS_NONE || keySystem == DRM::KS_WIDEVINE ||
         keySystem == DRM::KS_PLAYREADY || keySystem == DRM::KS_WISEPLAY;
}

bool DRM::WvUnwrapLicense(std::string wrapper,
                          std::string contentType,
                          std::string data,
                          std::string& dataOut,
                          int& hdcpLimit)
{
  // By default the license response should be in binary data format
  // but many services have a proprietary implementations therefore
  // the license data could be wrapped (such as base64, json, etc...)
  // here we provide the support for some common wrappers,
  // as alternative an add-on must implement a proxy where it can request
  // and process the license in a custom way and so return here the binary data

  std::vector<Wrapper> wrappers = TranslateWrapper(wrapper);

  std::map<std::string, std::string> params;

  bool isAuto = (wrappers.size() == 0 || wrappers.front() == Wrapper::AUTO) &&
                wrappers.front() != Wrapper::NONE;

  bool isAllowedFallbacks{false};

  if (isAuto)
  {
    wrappers.clear();
    // Check mime types to try detect the wrapper
    if (contentType == "application/octet-stream")
    {
      // its binary
    }
    else if (contentType == "application/json")
    {
      if (BASE64::IsBase64(data))
        wrappers.emplace_back(Wrapper::BASE64);

      wrappers.emplace_back(Wrapper::JSON);
    }
    else if (contentType == "application/xml" || contentType == "text/xml")
    {
      wrappers.emplace_back(Wrapper::XML);
    }
    else if (contentType == "text/plain")
    {
      // Some service use text mime type for XML
      isAllowedFallbacks = true;
      wrappers.emplace_back(Wrapper::XML);
    }
    else // Unknown
    {
      // Assumed to be binary with a possible base64 wrap
      if (BASE64::IsBase64(data))
        wrappers.emplace_back(Wrapper::BASE64);
    }
  }

  // Process multiple wrappers with sequential order

  for (size_t i = 0; i < wrappers.size(); ++i)
  {
    const auto& wrapper = wrappers[i];

    if (wrapper == Wrapper::NONE)
    {
      break;
    }
    else if (wrapper == Wrapper::BASE64)
    {
      data = BASE64::DecodeToStr(data);
    }
    else if (wrapper == Wrapper::JSON)
    {
      if (params["path_data"].empty())
      {
        LOG::LogF(LOGERROR,
                  "Cannot parse JSON license data, \"path_data\" parameter not specified");
        return false;
      }

      rapidjson::Document jDoc;
      jDoc.Parse(data.c_str(), data.size());

      if (!jDoc.IsObject())
      {
        LOG::LogF(LOGERROR,
                  "Unable to parse license data as JSON format, malformed data or wrong wrapper");
        return false;
      }

      rapidjson::Value* jDataObjValue = JSON::GetValueAtPath(jDoc, params["path_data"]);

      if (!jDataObjValue || !jDataObjValue->IsString())
      {
        LOG::LogF(LOGERROR, "Unable to get license data from JSON path, possible wrong path on "
                            "\"path_data\" parameter");
        return false;
      }

      data = jDataObjValue->GetString();

      if (!params["path_hdcp"].empty())
      {
        rapidjson::Value* jHdcpObjValue = JSON::GetValueAtPath(jDoc, params["path_hdcp"]);

        if (!jHdcpObjValue || !jHdcpObjValue->IsInt())
        {
          LOG::LogF(LOGERROR, "Unable to parse JSON HDCP path, possible wrong path or data type on "
                              "\"path_hdcp\" parameter");
        }
        else
          hdcpLimit = jHdcpObjValue->GetInt();
      }

      if (isAuto && BASE64::IsBase64(data))
        wrappers.emplace_back(Wrapper::BASE64);
    }
    else if (wrapper == Wrapper::XML)
    {
      if (params["path_data"].empty())
      {
        LOG::LogF(LOGERROR, "Cannot parse XML license data, \"path_data\" parameter not specified");
        return false;
      }

      xml_document doc;
      xml_parse_result parseRes = doc.load_buffer(data.c_str(), data.size());

      if (parseRes.status != status_ok)
      {
        if (isAllowedFallbacks)
        {
          LOG::LogF(LOGDEBUG, "License data not in XML format, fallback to binary");
          wrappers.emplace_back(Wrapper::NONE);
          continue;
        }
        else
        {
          LOG::LogF(LOGERROR, "Unable to parse XML license data, malformed data or wrong wrapper");
          return false;
        }
      }

      pugi::xml_node node = doc.select_node(params["path_data"].c_str()).node();
      if (!node)
      {
        LOG::LogF(LOGERROR, "Unable to get license data from XML path, possible wrong path on "
                            "\"path_data\" parameter");
        return false;
      }
      data = node.child_value();

      if (isAuto && BASE64::IsBase64(data))
        wrappers.emplace_back(Wrapper::BASE64);
    }
  }

  if (data.empty())
  {
    LOG::LogF(LOGERROR, "No license data, a problem occurred while processing license wrappers");
    return false;
  }

  //! @todo: the support to binary license data (with HB) that start with "\r\n\r\n" has not been reintroduced with the
  //! rework of this code, this is a very old unclear addition, seem there are no info about this on web,
  //! and seem no addons use it, so for now is removed, if in the future someone complain about this lack
  //! will be possible reintroduce it and include more clear info about this use case.
  // if (data.compare(0, 4, "\r\n\r\n") == 0)
  //   data.erase(0, 4);

  dataOut = data;

  return true;
}

std::string DRM::GenerateUrlDomainHash(std::string_view url)
{
  std::string baseDomain = URL::GetBaseDomain(url.data());
  // If we are behind a proxy we fall always in to the same domain e.g. "http://localhost/"
  // but we have to differentiate the results based on the service of the add-on hosting the proxy
  // to avoid possible collisions, so we include the first directory path after the domain name
  // e.g. http://localhost:1234/addonservicename/other_dir/get_license?id=xyz
  // domain will result: http://localhost/addonservicename/
  if (STRING::Contains(baseDomain, "127.0.0.1") || STRING::Contains(baseDomain, "localhost"))
  {
    const size_t domainStartPos = url.find("://") + 3;
    const size_t pathStartPos = url.find_first_of('/', domainStartPos);
    if (pathStartPos != std::string::npos)
    {
      // Try get the first directory path name
      const size_t nextSlashPos = url.find_first_of('/', pathStartPos + 1);
      size_t length = std::string::npos;
      if (nextSlashPos != std::string::npos)
      {
        length = nextSlashPos - pathStartPos;
        baseDomain += url.substr(pathStartPos, length);
      }
    }
  }

  // Generate hash of domain name
  DIGEST::MD5 md5;
  md5.Update(baseDomain.c_str(), static_cast<uint32_t>(baseDomain.size()));
  md5.Finalize();
  return md5.HexDigest();
}
