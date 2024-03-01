/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "utils/CurlUtils.h"

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

#include <mutex>
#include <string>
#include <unordered_set>

// forwards
namespace adaptive
{
class AdaptiveTree;
}

namespace ADP
{
namespace RESOURCES
{
class ATTR_DLL_LOCAL CCompResources
{
public:
  CCompResources() = default;
  ~CCompResources() = default;

  void InitStage2(adaptive::AdaptiveTree* tree) { m_tree = tree; }

  /*!
   * \brief Cookies that can be shared along with HTTP requests.
   *        Some video services require you to accept cookies and send cookies along with requests.
   *        Most common use case is when cookies are used as authentication to get files, so at the first HTTP request
   *        of the manifest, the server send a "Set-Cookies" header from HTTP response, which the client will have to use
   *        for each subsequent request, such as manifest update, segments, etc.
   *        NOTE: Read/write accesses must be protected by mutex.
   * \return The cookies
   */
  std::unordered_set<UTILS::CURL::Cookie>& Cookies() { return m_cookies; }

  std::mutex& GetCookiesMutex() { return m_cookiesMutex; }

  /*!
   * \brief Get the manifest tree.
   * \return The manifest tree.
   */
  const adaptive::AdaptiveTree& GetTree() const { return *m_tree; }

private:
  std::unordered_set<UTILS::CURL::Cookie> m_cookies;
  std::mutex m_cookiesMutex;
  adaptive::AdaptiveTree* m_tree{nullptr};
};
} // namespace RESOURCES
} // namespace ADP
