/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "samplereader/SampleReader.h"

#include <memory>

namespace PLAYLIST
{
enum class ContainerType;
}
namespace SESSION
{
class CStream;
}

namespace ADP
{

/*!
 * \brief Create and initialize a sample stream reader for the specified container
 * \param containerType The type of stream container, the value could be changed if the type is wrong
 * \param stream The stream object that will use the reader
 * \param streamId The stream id
 * \param includedStreamMask The flags for included streams
 * \return The sample reader if has success, otherwise nullptr
 */
std::unique_ptr<ISampleReader> CreateStreamReader(PLAYLIST::ContainerType& containerType,
                                                  SESSION::CStream* stream,
                                                  uint32_t streamId,
                                                  uint32_t includedStreamMask);

} // namespace ADP
