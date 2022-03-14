/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AdaptiveByteStream.h"

// AP4_ByteStream methods
AP4_Result CAdaptiveByteStream::ReadPartial(void* buffer, AP4_Size bytesToRead, AP4_Size& bytesRead)
{
  bytesRead = m_adStream->read(buffer, bytesToRead);
  return bytesRead > 0 ? AP4_SUCCESS : AP4_ERROR_READ_FAILED;
}

AP4_Result CAdaptiveByteStream::WritePartial(const void* buffer,
                                            AP4_Size bytesToWrite,
                                            AP4_Size& bytesWritten)
{
  /* unimplemented */
  return AP4_ERROR_NOT_SUPPORTED;
}

AP4_Result CAdaptiveByteStream::Seek(AP4_Position position)
{
  return m_adStream->seek(position) ? AP4_SUCCESS : AP4_ERROR_NOT_SUPPORTED;
}

AP4_Result CAdaptiveByteStream::Tell(AP4_Position& position)
{
  position = m_adStream->tell();
  return AP4_SUCCESS;
}

AP4_Result CAdaptiveByteStream::GetSize(AP4_LargeSize& size)
{
  return AP4_ERROR_NOT_SUPPORTED;
}

AP4_Result CAdaptiveByteStream::GetSegmentSize(size_t& size)
{
  if (m_adStream->ensureSegment() && m_adStream->retrieveCurrentSegmentBufferSize(size))
  {
    return AP4_SUCCESS;
  }
  return AP4_ERROR_EOS;
}
