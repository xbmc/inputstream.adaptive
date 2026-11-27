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

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace DRM
{

class ATTR_DLL_LOCAL CDRMEngine
{
public:
  CDRMEngine() = default;
  virtual ~CDRMEngine() = default;

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
   * \param canCleanupSessions Delete existing sessions before to create a new one
   * \return The DRM session if has success, otherwise nullptr
   */
  const std::shared_ptr<DRMSession> InitializeSession(std::vector<DRM::DRMInfo> manifestDrmInfos,
                                                      std::vector<DRM::DRMInfo> mediaDrmInfos,
                                                      DRM::DRMMediaType mediaType,
                                                      std::optional<bool> isForceSecureDecoder,
                                                      kodi::addon::InputstreamInfo& streamInfo,
                                                      bool canCleanupSessions);

  /*!
   * \brief Some live streams with in-band key rotation have an encrypted init segment
   *        without usable init data or default KID, only the media segments have the PSSH.
   *        In this case, insert as first media DRM info the one with the PSSH of the
   *        first media segment, to open the session with it.
   * \param manifestDrmInfos The DRM info provided by the manifest
   * \param mediaDrmInfos[in,out] The DRM info provided by the init segment
   * \param getPsshData Get the PSSH data of the first media segment for a DRM system ID,
   *                    empty if none
   * \return False if the PSSH of the media segment is needed but not found, otherwise true
   */
  bool AddMediaSegmentDrmInfo(
      const std::vector<DRM::DRMInfo>& manifestDrmInfos,
      std::vector<DRM::DRMInfo>& mediaDrmInfos,
      const std::function<std::vector<uint8_t>(const uint8_t* systemId)>& getPsshData) const;

  /*!
   * \brief Get the session by ID (dont take in account KID).
   * \param id The session ID
   * \return The session, otherwise nullptr if not found
   */
  const std::shared_ptr<DRMSession> GetSession(const std::string& id) const;

  /*!
   * \brief Get the session by ID that match the specified KID.
   * \param id The session ID
   * \return The session, otherwise nullptr if not found
   */
  const std::shared_ptr<DRMSession> GetSession(const std::string& id, const std::string& kid) const;


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
   * \brief Initialize the DRM engine.
   */
  bool Initialize();

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

  // \brief Delete all sessions by media type
  void DeleteSessionsByType(const DRMMediaType type);

  // \brief Check if a Key System is supported
  bool HasKeySystemSupport(std::string_view keySystem) const;

  // \brief Get a DRM instance for the specified key system, it will return nullptr if not found
  std::shared_ptr<DRM::IDecrypter> GetDrmInstance(std::string_view ks) const;

  /*!
   * \brief Get the DRM system ID whose PSSH must be read from the first media segment.
   * \param manifestDrmInfos The DRM info provided by the manifest
   * \param mediaDrmInfos The DRM info provided by the init segment
   * \return The system ID, nullptr if the PSSH of the media segment is not needed
   *         or the decrypter cannot renew the keys
   */
  const uint8_t* GetMediaSegmentPsshSystemId(const std::vector<DRM::DRMInfo>& manifestDrmInfos,
                                             const std::vector<DRM::DRMInfo>& mediaDrmInfos) const;

  std::string m_keySystem; // Choosen key system
  std::vector<DRMInstance> m_drms;
  std::vector<std::shared_ptr<DRMSession>> m_sessions;

  EngineStatus m_status{EngineStatus::NONE};
  bool m_isPreinitialized{false};
};

} // namespace DRM
