/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CodecParser.h"

#include <bento4/Ap4Utils.h>
using namespace adaptive;

AdtsType CAdaptiveAdtsHeaderParser::GetAdtsType(AP4_ByteStream* stream)
{
  AP4_DataBuffer buffer;
  buffer.SetDataSize(AP4_EAC3_HEADER_SIZE); // max header size is 64 (E-AC3)
  AdtsType adtsType = AdtsType::NONE;

  if (!AP4_SUCCEEDED(stream->Read(buffer.UseData(), AP4_EAC3_HEADER_SIZE)))
    return adtsType;

  AP4_BitReader bits(buffer.GetData(), AP4_EAC3_HEADER_SIZE);
  AP4_UI32 syncWord = bits.ReadBits(16);
  if ((syncWord & AP4_ADTS_SYNC_MASK) == AP4_ADTS_SYNC_PATTERN)
  {
    adtsType = AdtsType::AAC;
  }
  else if ((syncWord & AP4_AC4_SYNC_MASK) == AP4_AC4_SYNC_PATTERN)
  {
    adtsType = AdtsType::AC4;
  }
  else if (syncWord == AP4_AC3_SYNC_PATTERN)
  {
    bits.SkipBits(24);
    AP4_UI32 bitStreamID = bits.ReadBits(5);
    if ((bitStreamID > 10) && (bitStreamID <= 16))
    {
      adtsType = AdtsType::EAC3;
    }
    else if (bitStreamID <= 10)
    {
      adtsType = AdtsType::AC3;
    }
  }
  AP4_Position currentPos;
  stream->Tell(currentPos);
  stream->Seek(currentPos - (AP4_EAC3_HEADER_SIZE));
  return adtsType;
}

AP4_Result CAdaptiveAdtsParser::FindFrameHeader(AP4_AacFrame& frame)
{
  unsigned char raw_header[AP4_ADTS_HEADER_SIZE];
  AP4_Result result;

  /* align to the start of the next byte */
  m_Bits.ByteAlign();

  /* find a frame header */
  result = FindHeader(raw_header);
  if (AP4_FAILED(result))
    return result;

  /* parse the header */
  AP4_AdtsHeader adts_header(raw_header);

  /* check the header */
  result = adts_header.Check();
  if (AP4_FAILED(result))
    return AP4_ERROR_CORRUPTED_BITSTREAM;

  m_Bits.SkipBytes(AP4_ADTS_HEADER_SIZE);

  /* fill in the frame info */
  frame.m_Info.m_Standard =
      (adts_header.m_Id == 1 ? AP4_AAC_STANDARD_MPEG2 : AP4_AAC_STANDARD_MPEG4);
  switch (adts_header.m_ProfileObjectType)
  {
    case 0:
      frame.m_Info.m_Profile = AP4_AAC_PROFILE_MAIN;
      break;

    case 1:
      frame.m_Info.m_Profile = AP4_AAC_PROFILE_LC;
      break;

    case 2:
      frame.m_Info.m_Profile = AP4_AAC_PROFILE_SSR;
      break;

    case 3:
      frame.m_Info.m_Profile = AP4_AAC_PROFILE_LTP;
  }
  frame.m_Info.m_FrameLength = adts_header.m_FrameLength - AP4_ADTS_HEADER_SIZE;
  frame.m_Info.m_ChannelConfiguration = adts_header.m_ChannelConfiguration;
  frame.m_Info.m_SamplingFrequencyIndex = adts_header.m_SamplingFrequencyIndex;
  frame.m_Info.m_SamplingFrequency =
      AP4_AdtsSamplingFrequencyTable[adts_header.m_SamplingFrequencyIndex];

  /* skip crc if present */
  if (adts_header.m_ProtectionAbsent == 0)
  {
    m_Bits.SkipBits(16);
  }

  /* set the frame source */
  frame.m_Source = &m_Bits;

  return AP4_SUCCESS;
}

AP4_Result CAdaptiveAc3Parser::FindFrameHeader(AP4_Ac3Frame& frame)
{
  unsigned char raw_header[AP4_AC3_HEADER_SIZE];
  AP4_Result result;

  /* align to the start of the next byte */
  m_Bits.ByteAlign();

  /* find a frame header */
  result = FindHeader(raw_header);
  if (AP4_FAILED(result))
    return result;

  if (m_LittleEndian)
  {
    AP4_ByteSwap16(raw_header, AP4_AC3_HEADER_SIZE);
  }

  /* parse the header */
  AP4_Ac3Header ac3_header(raw_header);

  /* check the header */
  result = ac3_header.Check();
  if (AP4_FAILED(result))
  {
    m_Bits.SkipBytes(2);
    return AP4_ERROR_CORRUPTED_BITSTREAM;
  }

  frame.m_Info.m_ChannelCount = ac3_header.m_ChannelCount;
  frame.m_Info.m_SampleRate = FSCOD_AC3[ac3_header.m_Fscod];
  frame.m_Info.m_FrameSize = ac3_header.m_FrameSize;
  frame.m_Info.m_Ac3StreamInfo.fscod = ac3_header.m_Fscod;
  frame.m_Info.m_Ac3StreamInfo.bsid = ac3_header.m_Bsid;
  frame.m_Info.m_Ac3StreamInfo.bsmod = ac3_header.m_Bsmod;
  frame.m_Info.m_Ac3StreamInfo.acmod = ac3_header.m_Acmod;
  frame.m_Info.m_Ac3StreamInfo.lfeon = ac3_header.m_Lfeon;
  frame.m_Info.m_Ac3StreamInfo.bit_rate_code = ac3_header.m_Frmsizecod / 2;

  frame.m_LittleEndian = m_LittleEndian;

  /* set the frame source */
  frame.m_Source = &m_Bits;

  return AP4_SUCCESS;
}

AP4_Result CAdaptiveAc4Parser::FindFrameHeader(AP4_Ac4Frame& frame)
{
  unsigned int available;
  unsigned char raw_header[AP4_AC4_HEADER_SIZE];
  AP4_Result result;

  /* align to the start of the next byte */
  m_Bits.ByteAlign();

  /* find a frame header */
  result = FindHeader(raw_header);
  if (AP4_FAILED(result))
    return result;

  // duplicated work, just to get the frame size
  AP4_BitReader tmp_bits(raw_header, AP4_AC4_HEADER_SIZE);
  unsigned int sync_frame_size = GetSyncFrameSize(tmp_bits);
  if (sync_frame_size > (AP4_BITSTREAM_BUFFER_SIZE - 1))
  {
    return AP4_ERROR_NOT_ENOUGH_DATA;
  }

  /*
   * Error handling to skip the 'fake' sync word. 
   * - the maximum sync frame size is about (AP4_BITSTREAM_BUFFER_SIZE - 1) bytes.
   */
  if (m_Bits.GetBytesAvailable() < sync_frame_size)
  {
    if (m_Bits.GetBytesAvailable() == (AP4_BITSTREAM_BUFFER_SIZE - 1))
    {
      // skip the sync word, assume it's 'fake' sync word
      m_Bits.SkipBytes(2);
    }
    return AP4_ERROR_NOT_ENOUGH_DATA;
  }

  unsigned char* rawframe = new unsigned char[sync_frame_size];

  // copy the whole frame becasue toc size is unknown
  m_Bits.PeekBytes(rawframe, sync_frame_size);
  /* parse the header */
  AP4_Ac4Header ac4_header(rawframe, sync_frame_size);

  delete[] rawframe;

  // Place before goto statement to resolve Xcode compiler issue
  unsigned int bit_rate_mode = 0;

  /* check the header */
  result = ac4_header.Check();
  if (AP4_FAILED(result))
  {
    m_Bits.SkipBytes(sync_frame_size);
    goto fail;
  }

  /* check if we have enough data to peek at the next header */
  available = m_Bits.GetBytesAvailable();
  // TODO: find the proper AP4_AC4_MAX_TOC_SIZE or just parse what this step need ?
  if (available >= ac4_header.m_FrameSize + ac4_header.m_HeaderSize + ac4_header.m_CrcSize +
                       AP4_AC4_HEADER_SIZE + AP4_AC4_MAX_TOC_SIZE)
  {
    // enough to peek at the header of the next frame

    m_Bits.SkipBytes(ac4_header.m_FrameSize + ac4_header.m_HeaderSize + ac4_header.m_CrcSize);
    m_Bits.PeekBytes(raw_header, AP4_AC4_HEADER_SIZE);

    // duplicated work, just to get the frame size
    AP4_BitReader peak_tmp_bits(raw_header, AP4_AC4_HEADER_SIZE);
    unsigned int next_sync_frame_size = GetSyncFrameSize(peak_tmp_bits);

    unsigned char* next_rawframe = new unsigned char[next_sync_frame_size];

    // copy the whole frame becasue toc size is unknown
    if (m_Bits.GetBytesAvailable() < (next_sync_frame_size))
    {
      next_sync_frame_size = m_Bits.GetBytesAvailable();
    }
    m_Bits.PeekBytes(next_rawframe, next_sync_frame_size);

    m_Bits.SkipBytes(
        -((int)(ac4_header.m_FrameSize + ac4_header.m_HeaderSize + ac4_header.m_CrcSize)));

    /* check the header */
    AP4_Ac4Header peek_ac4_header(next_rawframe, next_sync_frame_size);

    delete[] next_rawframe;

    result = peek_ac4_header.Check();
    if (AP4_FAILED(result))
    {
      // TODO: need to reserve current sync frame ?
      m_Bits.SkipBytes(sync_frame_size + next_sync_frame_size);
      goto fail;
    }

    /* check that the fixed part of this header is the same as the */
    /* fixed part of the previous header                           */
    if (!AP4_Ac4Header::MatchFixed(ac4_header, peek_ac4_header))
    {
      // TODO: need to reserve current sync frame ?
      m_Bits.SkipBytes(sync_frame_size + next_sync_frame_size);
      goto fail;
    }
  }
  else if (available < (ac4_header.m_FrameSize + ac4_header.m_HeaderSize + ac4_header.m_CrcSize) ||
           (m_Bits.m_Flags & AP4_BITSTREAM_FLAG_EOS) == 0)
  {
    // not enough for a frame, or not at the end (in which case we'll want to peek at the next header)
    return AP4_ERROR_NOT_ENOUGH_DATA;
  }

  m_Bits.SkipBytes(ac4_header.m_HeaderSize);

  /* fill in the frame info */
  frame.m_Info.m_HeaderSize = ac4_header.m_HeaderSize;
  frame.m_Info.m_FrameSize = ac4_header.m_FrameSize;
  frame.m_Info.m_CRCSize = ac4_header.m_CrcSize;
  frame.m_Info.m_ChannelCount = ac4_header.m_ChannelCount;
  frame.m_Info.m_SampleDuration =
      (ac4_header.m_FsIndex == 0) ? 2048 : AP4_Ac4SampleDeltaTable[ac4_header.m_FrameRateIndex];
  frame.m_Info.m_MediaTimeScale =
      (ac4_header.m_FsIndex == 0) ? 44100 : AP4_Ac4MediaTimeScaleTable[ac4_header.m_FrameRateIndex];
  frame.m_Info.m_Iframe = ac4_header.m_BIframeGlobal;

  /* fill the AC4 DSI info */
  frame.m_Info.m_Ac4Dsi.ac4_dsi_version = 1;
  frame.m_Info.m_Ac4Dsi.d.v1.bitstream_version = ac4_header.m_BitstreamVersion;
  frame.m_Info.m_Ac4Dsi.d.v1.fs_index = ac4_header.m_FsIndex;
  frame.m_Info.m_Ac4Dsi.d.v1.fs =
      AP4_Ac4SamplingFrequencyTable[frame.m_Info.m_Ac4Dsi.d.v1.fs_index];
  frame.m_Info.m_Ac4Dsi.d.v1.frame_rate_index = ac4_header.m_FrameRateIndex;
  frame.m_Info.m_Ac4Dsi.d.v1.b_program_id = ac4_header.m_BProgramId;
  frame.m_Info.m_Ac4Dsi.d.v1.short_program_id = ac4_header.m_ShortProgramId;
  frame.m_Info.m_Ac4Dsi.d.v1.b_uuid = ac4_header.m_BProgramUuidPresent;
  AP4_CopyMemory(frame.m_Info.m_Ac4Dsi.d.v1.program_uuid, ac4_header.m_ProgramUuid, 16);

  // Calcuate the bit rate mode according to ETSI TS 103 190-2 V1.2.1 Annex B
  if (ac4_header.m_WaitFrames == 0)
  {
    bit_rate_mode = 1;
  }
  else if (ac4_header.m_WaitFrames >= 1 && ac4_header.m_WaitFrames <= 6)
  {
    bit_rate_mode = 2;
  }
  else if (ac4_header.m_WaitFrames > 6)
  {
    bit_rate_mode = 3;
  }

  frame.m_Info.m_Ac4Dsi.d.v1.ac4_bitrate_dsi.bit_rate_mode = bit_rate_mode;
  frame.m_Info.m_Ac4Dsi.d.v1.ac4_bitrate_dsi.bit_rate = 0; // unknown, fixed value now
  frame.m_Info.m_Ac4Dsi.d.v1.ac4_bitrate_dsi.bit_rate_precision =
      0xffffffff; // unknown, fixed value now
  frame.m_Info.m_Ac4Dsi.d.v1.n_presentations = ac4_header.m_NPresentations;
  frame.m_Info.m_Ac4Dsi.d.v1.presentations = ac4_header.m_PresentationV1;

  /* set the frame source */
  frame.m_Source = &m_Bits;

  return AP4_SUCCESS;

fail:
  /* skip the header and return (only skip the first byte in  */
  /* case this was a false header that hides one just after)  */
  return AP4_ERROR_CORRUPTED_BITSTREAM;
}

AP4_Result CAdaptiveEac3Parser::FindFrameHeader(AP4_Eac3Frame& frame)
{
  bool dependent_stream_exist = false;
  unsigned int dependent_stream_chan_loc = 0;
  unsigned int dependent_stream_length = 0;
  unsigned int skip_size = 0;
  unsigned char raw_header[AP4_EAC3_HEADER_SIZE];
  AP4_Result result;

  /* align to the start of the next byte */
  m_Bits.ByteAlign();

  /* find a frame header */
  result = FindHeader(raw_header, skip_size);
  if (AP4_FAILED(result))
    return result;

  if (m_LittleEndian)
  {
    AP4_ByteSwap16(raw_header, AP4_EAC3_HEADER_SIZE);
  }

  /* parse the header */
  AP4_Eac3Header eac3_header(raw_header);

  /* check the header */
  result = eac3_header.Check();
  if (AP4_FAILED(result))
    return AP4_ERROR_CORRUPTED_BITSTREAM;

  /* fill in the frame info */
  frame.m_Info.m_ChannelCount = eac3_header.m_ChannelCount;
  if (dependent_stream_exist)
  {
    frame.m_Info.m_FrameSize = eac3_header.m_FrameSize + dependent_stream_length;
  }
  else
  {
    frame.m_Info.m_FrameSize = eac3_header.m_FrameSize;
  }
  frame.m_Info.m_SampleRate = EAC3_SAMPLE_RATE_ARY[eac3_header.m_Fscod];
  frame.m_Info.m_Eac3SubStream.fscod = eac3_header.m_Fscod;
  frame.m_Info.m_Eac3SubStream.bsid = eac3_header.m_Bsid;
  frame.m_Info.m_Eac3SubStream.bsmod = eac3_header.m_Bsmod;
  frame.m_Info.m_Eac3SubStream.acmod = eac3_header.m_Acmod;
  frame.m_Info.m_Eac3SubStream.lfeon = eac3_header.m_Lfeon;
  if (dependent_stream_exist)
  {
    frame.m_Info.m_Eac3SubStream.num_dep_sub = 1;
    frame.m_Info.m_Eac3SubStream.chan_loc = dependent_stream_chan_loc;
  }
  else
  {
    frame.m_Info.m_Eac3SubStream.num_dep_sub = 0;
    frame.m_Info.m_Eac3SubStream.chan_loc = 0;
  }

  frame.m_Info.complexity_index_type_a = 0;
  if (eac3_header.m_Addbsie && (eac3_header.m_Addbsil == 1) && (eac3_header.m_Addbsi[0] == 0x1))
  {
    frame.m_Info.complexity_index_type_a = eac3_header.m_Addbsi[1];
  }

  /* set the little endian flag */
  frame.m_LittleEndian = m_LittleEndian;

  /* set the frame source */
  frame.m_Source = &m_Bits;

  return AP4_SUCCESS;
}
