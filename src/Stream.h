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

#include <optional>

namespace SESSION
{
class ATTR_DLL_LOCAL CStream
{
public:
  CStream(adaptive::AdaptiveTree* tree,
          PLAYLIST::CAdaptationSet* adp,
          PLAYLIST::CRepresentation* initialRepr)
    : m_adStream{tree, adp, initialRepr}, m_isValid{true}
  {
  }

  ~CStream() { Disable(); }

  /*!
   * \brief Determines whether the stream is enabled for playback
   * \return True if the stream is enabled, otherwise false
   */
  bool IsEnabled() const { return m_isEnabled; }

  /*!
   * \brief Set if the stream is enabled for playback
   * \param isEnabled Set to true to enable the stream
   */
  void SetIsEnabled(bool isEnabled) { m_isEnabled = isEnabled; };

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

  std::optional<unsigned int> m_mainStreamIndex; // Used when this stream is "included" to video (main stream)
  adaptive::AdaptiveStream m_adStream;
  kodi::addon::InputstreamInfo m_info;
  bool m_isValid;

private:
  bool m_isEnabled{false};
  std::unique_ptr<ISampleReader> m_streamReader;
  std::unique_ptr<CAdaptiveByteStream> m_adByteStream;
  std::unique_ptr<AP4_File> m_streamFile;
};
} // namespace SESSION
