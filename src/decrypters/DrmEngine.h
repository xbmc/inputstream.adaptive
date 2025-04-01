/*
 *  Copyright (C) 2025 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "DrmEngineDefines.h"
#include "IDecrypter.h"
#include "utils/CryptoUtils.h"

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/addon-instance/Inputstream.h>
#endif

#include <memory>
#include <optional>
#include <string>
#include <vector>

// forwards
namespace PLAYLIST
{
class CRepresentation;
class CAdaptationSet;
} // namespace PLAYLIST

namespace DRM
{

class ATTR_DLL_LOCAL CDRMEngine
{
public:
  CDRMEngine() = default;
  virtual ~CDRMEngine() = default;

  /*!
   * \brief Initialize the DRM engine.
   */
  void Initialize();

  /*!
   * \brief Pre-initialize a DRM before to download a manifest and create a session.
   * \param session[OUT] The opened session
   * \return True if has been pre-initialized, otherwise false
   */
  bool PreInitializeDRM(DRMSession& session);

  /*!
   * \brief Initialize a DRM (if needed), then create or reuse a session.
   * \param drmInfos The DRM info provided by a manifest
   * \param mediaType The type of media involved in the session
   * \param isForceSecureDecoder[OPT] To force the Secure Decoder, this setting coming from the manifest
   * \param streamInfo The InputstreamInfo where set the DRM configuration
   * \param repr The representation of the stream refer to drmInfos
   * \param adp The adaptation set of the representation
   * \param canCleanupSessions Delete existing sessions before to create a new one
   * \return True if has success, otherwise false
   */
  bool InitializeSession(std::vector<DRM::DRMInfo> drmInfos,
                         DRM::DRMMediaType mediaType,
                         std::optional<bool> isForceSecureDecoder,
                         kodi::addon::InputstreamInfo& streamInfo,
                         PLAYLIST::CRepresentation* repr,
                         PLAYLIST::CAdaptationSet* adp,
                         bool canCleanupSessions);

  /*!
   * \brief Get the session by ID.
   * \param id The session ID
   * \return The session, otherwise nullptr if not found
   */
  const DRMSession* GetSession(const std::string& id) const;

  /*!
   * \brief Get the current engine status. The state can change after each call to InitializeSession.
   * \return The current status
   */
  EngineStatus GetStatus() const { return m_status; }

  /*!
   * \brief Unload DRM engine resources.
   */
  void Dispose();

private:
  /*!
   * \brief Configure DRM ClearKey, by replacing manifest DRM info when needed.
   */
  bool ConfigureClearKey(std::vector<DRM::DRMInfo>& drmInfos);

  /*!
   * \brief Select and set a DRM compatible with a DRM info.
   * \param drmInfos The manifest DRM info
   * \return True if a match was found, otherwise false
   */
  bool SelectDRM(std::vector<DRM::DRMInfo>& drmInfos);

  void ExtractStreamProtectionData(PLAYLIST::CRepresentation* repr,
                                   PLAYLIST::CAdaptationSet* adp,
                                   DRM::DRMInfo& drmInfo);

  // \brief Delete all sessions by media type
  void DeleteSessionsByType(const DRMMediaType type);

  // \brief Check if a Key System is supported
  bool HasKeySystemSupport(std::string_view keySystem) const;

  std::vector<std::string> m_supportedKs; // Supported key systems, the lower index has higher priority
  std::string m_keySystem; // Choosen key system
  std::map<std::string, std::shared_ptr<DRM::IDecrypter>> m_drms; // KeySystem - DRM instance
  
  uint32_t m_sessionsCounter{1}; // Sessions counter to generate session ID
  std::vector<DRMSession> m_sessions;
  
  EngineStatus m_status{EngineStatus::NONE};
  bool m_isPreinitialized{false};
};

} // namespace DRM
