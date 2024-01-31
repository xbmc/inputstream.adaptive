/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <string>
#include <string_view>

namespace UTILS
{
namespace URL
{
/*! \brief Check if it is a valid URL
 *  \return True if it is a valid URL, false otherwise
 */
bool IsValidUrl(const std::string& url);

/*! \brief Check if it is an absolute URL
 *  \return True if it is an absolute URL, false otherwise
 */
bool IsUrlAbsolute(std::string_view url);

/*! \brief Check if it is a relative URL e.g. "/something/"
 *  \param url An URL
 *  \return True if it is a relative URL, false otherwise
 */
bool IsUrlRelative(std::string_view url);

/*! \brief Check if it is a relative URL to a level e.g. "../something/"
 *  \param url An URL
 *  \return True if it is a relative URL to a level, false otherwise
 */
bool IsUrlRelativeLevel(std::string_view url);

/*! \brief Get URL parameters starting from the parameter referred to the value
 *   placeholder until the end. E.g. with placeholder "$START_NUMBER$"
 *   from "https://foo.bar/dash.mpd?start_seq=$START_NUMBER$"
 *   will return "?start_seq=$START_NUMBER$"
 *  \param url An URL with parameteres
 *  \param placeholder The value placeholder name of a parameter
 *  \return The parameters referred to the placeholder until the end of url
 */
std::string GetParametersFromPlaceholder(std::string& url, std::string_view placeholder);

/*! \brief Get URL parameters e.g. "?q=something"
 *  \param url An URL
 *  \return The URL parameters
 */
std::string GetParameters(std::string& url);

/*! \brief Remove URL parameters e.g. "?q=something"
 *  \param url An URL
 *  \param removeFilenameParam If true remove the last URL level if it
 *   contains a filename with extension
 *  \return The URL without parameters
 */
std::string RemoveParameters(std::string url, bool removeFilenameParam = true);

/*! \brief Append a string of parameters to an URL without overwriting existing params values
 *  \param url URL where append the parameters
 *  \param params Params to be appended
 */
void AppendParameters(std::string& url, std::string_view params);

/*! \brief Get the domain URL from an URL
 *  \param url An URL
 *  \return The domain URL
 */
std::string GetDomainUrl(std::string url);

/*! 
 * \brief Combine two URLs as per RFC 3986 specification.
 * \param baseUrl The base URL, absolute or relative.
 * \param relativeUrl The other relative URL to be combined.
 * \return The final URL.
 */
std::string Join(std::string baseUrl, std::string relativeUrl);

/*!
 * \brief Ensure that the URL address ends with backslash "/".
 * \param url[IN][OUT] An URL.
 */
void EnsureEndingBackslash(std::string& url);

} // namespace URL
} // namespace UTILS
