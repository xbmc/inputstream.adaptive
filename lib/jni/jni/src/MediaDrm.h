/*
 *  Copyright (C) 2013 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "JNIBase.h"
#include "MediaDrmKeyRequest.h"
#include "MediaDrmProvisionRequest.h"
#include <map>

namespace jni
{

class CJNIUUID;
class CJNIMediaDrmOnEventListener;

class CJNIMediaDrm : public CJNIBase
{
public:
  static const int KEY_TYPE_STREAMING = 1;
  static const int KEY_TYPE_OFFLINE = 2;
  static const int KEY_TYPE_RELEASE = 3;

  static const int EVENT_PROVISION_REQUIRED = 1;
  static const int EVENT_KEY_REQUIRED = 2;
  static const int EVENT_KEY_EXPIRED = 3;
  static const int EVENT_VENDOR_DEFINED = 4;
  static const int EVENT_SESSION_RECLAIMED = 5;

  CJNIMediaDrm(const jni::jhobject &object) : CJNIBase(object) {};
  CJNIMediaDrm(const CJNIUUID& uuid);
  ~CJNIMediaDrm() {};

  void release() const;

  std::vector<char> openSession() const;
  void closeSession(const std::vector<char> & sessionId) const;

  std::string getPropertyString(const std::string &propertyName) const;
  std::vector<uint8_t> getPropertyByteArray(const std::string& propertyName) const;
  void setPropertyString(const std::string &propertyName, const std::string &value) const;
  void setPropertyByteArray(const std::string &propertyName, const std::vector<uint8_t> &value) const;

  CJNIMediaDrmKeyRequest getKeyRequest(const std::vector<char> &scope,
    const std::vector<uint8_t> &init, const std::string &mimeType, int keyType,
    const std::map<std::string, std::string> &optionalParameters) const;
  std::vector<char> provideKeyResponse(const std::vector<char> &scope, const std::vector<char> &response) const;

  CJNIMediaDrmProvisionRequest getProvisionRequest() const;
  void provideProvisionResponse(const std::vector<uint8_t> &response) const;

  void removeKeys(const std::vector<char> &sessionId) const;

  void setOnEventListener(const CJNIMediaDrmOnEventListener &listener) const;

  std::map<std::string, std::string> queryKeyStatus(const std::vector<char> &sessionId) const;

  int getSecurityLevel(const std::vector<char> &sessionId) const;
  int getMaxSecurityLevel() const;
};

}

