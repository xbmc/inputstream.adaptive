/*
 *  Copyright (C) 2019 peak3d (http://www.peak3d.de)
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <string>

#include <kodi/AddonBase.h>

namespace adaptive
{

class ATTR_DLL_LOCAL PRProtectionParser
{
public:
  PRProtectionParser(std::string wrmheader);
  std::string getKID() const { return m_strKID; };
  std::string getLicenseURL() const { return m_strLicenseURL; };
  std::string getPSSH() const { return m_strPSSH; };

  void setKID(const std::string kid) { m_strKID = kid; };
  void setLicenseURL(const std::string licenseURL) { m_strLicenseURL = licenseURL; };

  std::string m_strXMLText;

private:
  std::string m_strKID;
  std::string m_strLicenseURL;
  std::string m_strPSSH;
  };

  } // namespace adaptive
