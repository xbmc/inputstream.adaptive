/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#ifdef INPUTSTREAM_TEST_BUILD
#include "test/KodiStubs.h"
#else
#include <kodi/AddonBase.h>
#endif

#include <cstdint>
#include <memory>
#include <vector>

enum class CdmMessageType
{
  UNKNOWN,
  SESSION_MESSAGE,
  SESSION_KEY_CHANGE,
  EVENT_KEY_REQUIRED,
};

struct CdmMessage
{
  std::string sessionId;
  CdmMessageType type{CdmMessageType::UNKNOWN};
  std::vector<uint8_t> data;
  uint32_t status{0};
};

class ATTR_DLL_LOCAL IWVObserver // Observer called by IWVSubject interface
{
public:
  virtual ~IWVObserver() = default;
  virtual void OnNotify(const CdmMessage& message) = 0;
};

class ATTR_DLL_LOCAL IWVSubject // Subject to make callbacks to IWVObserver interfaces
{
public:
  virtual ~IWVSubject() = default;
  virtual void AttachObserver(IWVObserver* observer) = 0;
  virtual void DetachObserver(IWVObserver* observer) = 0;
  virtual void NotifyObservers(const CdmMessage& message) = 0;
};

template<class T>
class ATTR_DLL_LOCAL IWVCdmAdapter : public IWVSubject
{
public:
  virtual ~IWVCdmAdapter() = default;

  virtual std::shared_ptr<T> GetCDM() = 0;

  virtual const std::string& GetLicenseUrl() = 0;

  virtual void SetCodecInstance(void* instance) {}
  virtual void ResetCodecInstance() {}

  virtual std::string_view GetKeySystem() = 0;

  virtual std::string_view GetLibraryPath() const { return ""; }
  //! @todo: add here this method for convenience needed investigate better to better cleanup,
  //! also Load/Save certificate methods need a full code cleanup
  virtual void SaveServiceCertificate() {}
};

namespace DRM
{

/*!
 * \brief Generate a synthesized Widevine PSSH.
 *        (WidevinePsshData as google protobuf format
 *        https://github.com/devine-dl/pywidevine/blob/master/pywidevine/license_protocol.proto)
 * \param kid The KeyId
 * \param contentIdData Custom content for the "content_id" field as bytes
 *                      Placeholders allowed:
 *                      {KID} To inject the KID as bytes
 *                      {UUID} To inject the KID as UUID string format
 * \return The pssh if has success, otherwise empty value.
 */
std::vector<uint8_t> MakeWidevinePsshData(const std::vector<std::vector<uint8_t>>& keyIds,
                                          std::vector<uint8_t> contentIdData);

/*!
 * \brief Generate a synthesized Widevine PSSH.
 *        (WidevinePsshData as google protobuf format
 *        https://github.com/devine-dl/pywidevine/blob/master/pywidevine/license_protocol.proto)
 * \param kid The KeyId
 * \param contentIdData Custom content for the "content_id" field as bytes
 *                      Placeholders allowed:
 *                      {KID} To inject the KID as bytes
 *                      {UUID} To inject the KID as UUID string format
 * \return The pssh if has success, otherwise empty value.
 */
void ParseWidevinePssh(const std::vector<uint8_t>& wvPsshData,
                       std::vector<std::vector<uint8_t>>& keyIds);

} // namespace DRM
