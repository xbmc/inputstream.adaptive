/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AdaptiveByteStream.h"
#include "common/AdaptiveStream.h"
#include "samplereader/SampleReader.h"

#include <bento4/Ap4.h>
#include <kodi/addon-instance/Inputstream.h>

namespace SESSION
{
class ATTR_DLL_LOCAL CStream
{
public:
  CStream(adaptive::AdaptiveTree& tree,
          PLAYLIST::CAdaptationSet* adp,
          PLAYLIST::CRepresentation* initialRepr,
          const UTILS::PROPERTIES::KodiProperties& kodiProps,
          bool chooseRep)
    : m_isEnabled{false},
      m_isEncrypted{false},
      m_mainId{0},
      m_adStream{tree, adp, initialRepr, kodiProps, chooseRep},
      m_hasSegmentChanged{false},
      m_isValid{true} {};


  ~CStream() { Disable(); };

  /*!
   * \brief Stop/disable the AdaptiveStream and reset
   */
  void Disable();

  /*!
   * \brief Reset the stream components in preparation for opening a new stream
   */
  void Reset();

  /*!
   * \brief Get the stream sample reader pointer
   * \return The sample reader, otherwise nullptr if not set
   */
  ISampleReader* GetReader() const { return m_streamReader.get(); }

  /*!
   * \brief Set the stream sample reader
   * \param reader The reader
   */
  void SetReader(std::unique_ptr<ISampleReader> reader);

  /*!
   * \brief Get the stream file handler pointer
   * \return The stream file handler, otherwise nullptr if not set
   */
  AP4_File* GetStreamFile() const { return m_streamFile.get(); }

  /*!
   * \brief Set the stream file handler
   * \param streamFile The stream file handler
   */
  void SetStreamFile(std::unique_ptr<AP4_File> streamFile) { m_streamFile = std::move(streamFile); }

  /*!
   * \brief Get the adaptive byte stream handler pointer
   * \return The adaptive byte stream handler, otherwise nullptr if not set
   */
  CAdaptiveByteStream* GetAdByteStream() const { return m_adByteStream.get(); }

  /*!
   * \brief Set the adaptive byte stream handler
   * \param dataStream The adaptive byte stream handler
   */
  void SetAdByteStream(std::unique_ptr<CAdaptiveByteStream> adByteStream)
  {
    m_adByteStream = std::move(adByteStream);
  }

  bool m_isEnabled;
  bool m_isEncrypted;
  uint16_t m_mainId;
  adaptive::AdaptiveStream m_adStream;
  kodi::addon::InputstreamInfo m_info;
  bool m_hasSegmentChanged;
  bool m_isValid;

private:
  std::unique_ptr<ISampleReader> m_streamReader;
  std::unique_ptr<CAdaptiveByteStream> m_adByteStream;
  std::unique_ptr<AP4_File> m_streamFile;
};
} // namespace SESSION
