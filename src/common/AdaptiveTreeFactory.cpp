/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AdaptiveTreeFactory.h"

#include "parser/DASHTree.h"
#include "parser/HLSTree.h"
#include "parser/SmoothTree.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

using namespace PLAYLIST;
using namespace UTILS;

adaptive::AdaptiveTree* PLAYLIST_FACTORY::CreateAdaptiveTree(
    const UTILS::PROPERTIES::KodiProperties& kodiProps,
    const UTILS::CURL::HTTPResponse& manifestResp)
{
  // Add-on can override manifest type
  //! @todo: deprecated, to be removed on next Kodi release
  PROPERTIES::ManifestType manifestType = kodiProps.m_manifestType;

  // Detect the manifest type
  if (kodiProps.m_manifestType == PROPERTIES::ManifestType::UNKNOWN)
  {
    std::string contentType;
    if (STRING::KeyExists(manifestResp.headers, "content-type"))
      contentType = manifestResp.headers.at("content-type");

    manifestType = InferManifestType(manifestResp.effectiveUrl, contentType, manifestResp.data);
  }

  if (manifestType == PROPERTIES::ManifestType::UNKNOWN)
  {
    LOG::LogF(LOGERROR,
              "Cannot detect the manifest type.\n"
              "Check if the content-type header is correctly provided in the manifest response.");
    return nullptr;
  }

  switch (manifestType)
  {
    case PROPERTIES::ManifestType::MPD:
      return new adaptive::CDashTree();
      break;
    case PROPERTIES::ManifestType::ISM:
      return new adaptive::CSmoothTree();
      break;
    case PROPERTIES::ManifestType::HLS:
      return new adaptive::CHLSTree();
      break;
    default:
      LOG::LogF(LOGFATAL, "Manifest type %i not handled", manifestType);
  };

  return nullptr;
}

UTILS::PROPERTIES::ManifestType PLAYLIST_FACTORY::InferManifestType(std::string_view url,
                                                                    std::string_view contentType,
                                                                    std::string_view data)
{
  // Try detect manifest type by using mime type specified by the server
  if (contentType == "application/dash+xml")
    return PROPERTIES::ManifestType::MPD;
  else if (contentType == "vnd.apple.mpegurl" || contentType == "application/vnd.apple.mpegurl" ||
           contentType == "application/x-mpegURL")
    return PROPERTIES::ManifestType::HLS;
  else if (contentType == "application/vnd.ms-sstr+xml")
    return PROPERTIES::ManifestType::ISM;

  // Try detect manifest type by checking file extension
  std::string ext = STRING::ToLower(FILESYS::GetFileExtension(url.data()));
  if (ext == "mpd")
    return PROPERTIES::ManifestType::MPD;
  else if (ext == "m3u8")
    return PROPERTIES::ManifestType::HLS;
  else if (ext == "ism/manifest" || ext == "isml/manifest" || ext == "ism" || ext == "isml")
    return PROPERTIES::ManifestType::ISM;

  // Usually we could fall here if add-ons use a proxy to manipulate manifests without providing the
  // appropriate content-type header in the proxy HTTP response and by using also a custom address,
  // then as last resort we try detect the manifest type by parsing manifest data
  if (!data.empty())
  {
    // Try check if we have the optional BOM for UTF16 BE / LE data
    // in this case always assumes as Smooth Streaming manifest
    if ((data[0] == '\xFE' && data[1] == '\xFF') || (data[0] == '\xFF' && data[1] == '\xFE'))
    {
      return PROPERTIES::ManifestType::ISM;
    }

    // Since the data may be very large, limit the parsing to the first 200 characters
    std::string_view dataSnippet = data.substr(0, 200);
    if (dataSnippet.find("<MPD") != std::string::npos)
      return PROPERTIES::ManifestType::MPD;
    else if (dataSnippet.find("#EXTM3U") != std::string::npos)
      return PROPERTIES::ManifestType::HLS;
    else if (dataSnippet.find("SmoothStreamingMedia") != std::string::npos)
      return PROPERTIES::ManifestType::ISM;
  }

  return PROPERTIES::ManifestType::UNKNOWN;
}
