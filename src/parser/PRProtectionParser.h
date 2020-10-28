/*
*      Copyright (C) 2019 peak3d
*      http://www.peak3d.de
*
*  This Program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This Program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  <http://www.gnu.org/licenses/>.
*
*/

#pragma once

#include <string>

#include <kodi/AddonBase.h>

namespace adaptive
{

class ATTRIBUTE_HIDDEN PRProtectionParser
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
