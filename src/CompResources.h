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

#include <atomic>
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
struct ScreenInfo
{
  ScreenInfo() = default;
  ScreenInfo(int widthPx, int heightPx, int maxWidthPx, int maxHeightPx)
  {
    width = widthPx;
    height = heightPx;
    maxWidth = maxWidthPx;
    maxHeight = maxHeightPx;
  }
  int width{0};
  int height{0};
  int maxWidth{0};
  int maxHeight{0};
};

class ATTR_DLL_LOCAL CCompResources
{
public:
  CCompResources() = default;
  ~CCompResources() = default;

  void InitStage2(adaptive::AdaptiveTree* tree) { m_tree = tree; }

  /*!
   * \brief Get the current screen info.
   * \return The screen info.
   */
  ScreenInfo GetScreenInfo()
  {
    std::lock_guard<std::mutex> lock(m_screenInfoMutex);
    return m_screenInfo;
  }

  /*!
   * \brief Set the screen info.
   * \param screenInfo The scren info
   */
  void SetScreenInfo(const ScreenInfo& screenInfo)
  {
    std::lock_guard<std::mutex> lock(m_screenInfoMutex);
    m_screenInfo = screenInfo;
  }

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
  ScreenInfo m_screenInfo;
  std::mutex m_screenInfoMutex;
  std::unordered_set<UTILS::CURL::Cookie> m_cookies;
  std::mutex m_cookiesMutex;
  adaptive::AdaptiveTree* m_tree{nullptr};
};
} // namespace RESOURCES
} // namespace ADP
