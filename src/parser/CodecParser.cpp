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
