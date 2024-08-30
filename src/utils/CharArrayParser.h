/*
 *  Copyright (C) 2005-2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UTILS
{

/*!
 *\brief Wraps a char array, providing a set of methods for parsing data from it.
 */
class CCharArrayParser
{
public:
  CCharArrayParser() = default;
  ~CCharArrayParser() = default;

  /*!
   * \brief Sets the position and limit to zero
   */
  void Reset();

  /*!
   * \brief Updates the instance to wrap the specified data and resets the position to zero
   * \param data The data
   * \param limit The limit of length of the data
   */
  void Reset(const uint8_t* data, size_t limit);

  /*!
   * \brief Return the number of chars yet to be read
   */
  size_t CharsLeft();

  /*!
   * \brief Returns the current offset in the array
   */
  size_t GetPosition();

  /*!
   * \brief Set the reading offset in the array
   * \param position The new offset position
   * \return True if success, otherwise false
   */
  bool SetPosition(size_t position);

  /*!
   * \brief Skip a specified number of chars
   * \param nChars The number of chars
   * \return True if success, otherwise false
   */
  bool SkipChars(size_t nChars);

  /*!
   * \brief Reads the next unsigned char (it is assumed that the caller has
   * already checked the availability of the data for its length)
   * \return The unsigned char value
   */
  uint8_t ReadNextUnsignedChar();

  /*!
   * \brief Reads the next two chars as unsigned short value (it is assumed
   * that the caller has already checked the availability of the data for its length)
   * \return The unsigned short value
   */
  uint16_t ReadNextUnsignedShort();

  /*!
   * \brief Reads the next two chars little endian as unsigned short value (it is assumed
   * that the caller has already checked the availability of the data for its length)
   * \return The unsigned short value
   */
  uint16_t ReadLENextUnsignedShort();

  /*!
   * \brief Reads the next three chars as unsigned int 24bit value (it is assumed 
   * that the caller has already checked the availability of the data for its length)
   * \return The unsigned int24 converted to uint32_t
   */
  uint32_t ReadNextUnsignedInt24();

  /*!
   * \brief Reads the next four chars as unsigned int value (it is assumed 
   * that the caller has already checked the availability of the data for its length)
   * \return The unsigned int value
   */
  uint32_t ReadNextUnsignedInt();

  /*!
   * \brief Reads the next four chars little endian as unsigned short value (it is assumed
   * that the caller has already checked the availability of the data for its length)
   * \return The unsigned int value
   */
  uint32_t ReadNextLEUnsignedInt();

  /*!
   * \brief Reads the next eight chars as unsigned int64 value (it is assumed 
   * that the caller has already checked the availability of the data for its length)
   * \return The unsigned int64 value
   */
  uint64_t ReadNextUnsignedInt64();

  /*!
   * \brief Reads the next string of specified length (it is assumed that
   * the caller has already checked the availability of the data for its length)
   * \param length The length to be read
   * \return The string value
   */
  std::string ReadNextString(size_t length);

  /*!
   * \brief Reads the next chars array of specified length (it is assumed that
   * the caller has already checked the availability of the data for its length)
   * \param length The length to be read
   * \param data[OUT] The data read
   * \return True if success, otherwise false
   */
  bool ReadNextArray(size_t length, std::vector<uint8_t>& data);

  /*!
   * \brief Get the current data
   * \return The char pointer to the current data
   */
  const uint8_t* GetData() { return m_data; };

  /*!
   * \brief Get the data from current position
   * \return The char pointer from the current data position
   */
  const uint8_t* GetDataPos() { return m_data + m_position; }

private:
  const uint8_t* m_data{nullptr};
  size_t m_position{0};
  size_t m_limit{0};
};

} // namespace UTILS
