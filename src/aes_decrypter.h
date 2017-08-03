/*
*      Copyright (C) 2016-2016 peak3d
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

#include "Ap4Types.h"
#include <string>

class AESDecrypter
{
public:
  AESDecrypter() = default;
  virtual ~AESDecrypter() = default;

  void decrypt(const AP4_UI08 *aes_key, const AP4_UI08 *aes_iv, std::string &data);
  std::string convertIV(const std::string &input);
  void ivFromSequence(uint8_t *buffer, uint64_t sid);
private:
  std::string m_swapBuffer;
};
