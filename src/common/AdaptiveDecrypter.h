/*
*      Copyright (C) 2022 Team Kodi
*      This file is part of Kodi - https://kodi.tv
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

#include "Ap4.h"

#include <stdexcept>
#include <string>

class Adaptive_CencSingleSampleDecrypter : public AP4_CencSingleSampleDecrypter
{
public:
  Adaptive_CencSingleSampleDecrypter() : AP4_CencSingleSampleDecrypter(nullptr){};

  /*! \brief Add a Key ID to the current session
   *  \param keyId The KID
   */
  virtual void AddKeyId(const std::string& keyId)
  {
    throw std::logic_error("AddKeyId method not implemented.");
  };

  /*! \brief Set a Key ID as default
   *  \param keyId The KID
   */
  virtual void SetDefaultKeyId(const std::string& keyId)
  {
    throw std::logic_error("SetDefaultKeyId method not implemented.");
  };
};
