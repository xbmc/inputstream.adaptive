/*
 *      Copyright (C) 2013 Jean-Luc Barriere
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301 USA
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "tsDemuxer.h"
#include "ES_MPEGVideo.h"
#include "ES_MPEGAudio.h"
#include "ES_h264.h"
#include "ES_hevc.h"
#include "ES_AAC.h"
#include "ES_AC3.h"
#include "ES_Subtitle.h"
#include "ES_Teletext.h"
#include "debug.h"

#include <cassert>

#define MAX_RESYNC_SIZE         65536

using namespace TSDemux;

AVContext::AVContext(TSDemuxer* const demux, uint64_t pos, uint16_t channel)
  : av_pos(pos)
  , payload_unit_pos(0)
  , av_data_len(FLUTS_NORMAL_TS_PACKETSIZE)
  , av_pkt_size(0)
  , is_configured(false)
  , channel(channel)
  , pid(0xffff)
  , transport_error(false)
  , has_payload(false)
  , payload_unit_start(false)
  , discontinuity(false)
  , payload(NULL)
  , payload_len(0)
  , packet(NULL)
{
  m_demux = demux;
  memset(av_buf, 0, sizeof(av_buf));
};

void AVContext::Reset(void)
{
  PLATFORM::CLockObject lock(mutex);

  pid = 0xffff;
  transport_error = false;
  has_payload = false;
  payload_unit_start = false;
  discontinuity = false;
  payload = NULL;
  payload_len = 0;
  payload_unit_pos = 0;
  packet = NULL;
}

uint16_t AVContext::GetPID() const
{
  return pid;
}

PACKET_TYPE AVContext::GetPIDType() const
{
  PLATFORM::CLockObject lock(mutex);

  if (packet)
    return packet->packet_type;
  return PACKET_TYPE_UNKNOWN;
}

uint16_t AVContext::GetPIDChannel() const
{
  PLATFORM::CLockObject lock(mutex);

  if (packet)
    return packet->channel;
  return 0xffff;
}

bool AVContext::HasPIDStreamData() const
{
  PLATFORM::CLockObject lock(mutex);

  // PES packets append frame buffer of elementary stream until next start of unit
  // On new unit start, flag is held
  if (packet && packet->has_stream_data)
    return true;
  return false;
}

bool AVContext::HasPIDPayload() const
{
  return has_payload;
}

ElementaryStream* AVContext::GetPIDStream()
{
  PLATFORM::CLockObject lock(mutex);

  if (packet && packet->packet_type == PACKET_TYPE_PES)
    return packet->stream;
  return NULL;
}

std::vector<ElementaryStream*> AVContext::GetStreams()
{
  PLATFORM::CLockObject lock(mutex);

  std::vector<ElementaryStream*> v;
  for (std::map<uint16_t, Packet>::iterator it = packets.begin(); it != packets.end(); ++it)
    if (it->second.packet_type == PACKET_TYPE_PES && it->second.stream)
      v.push_back(it->second.stream);
  return v;
}

void AVContext::StartStreaming(uint16_t pid)
{
  PLATFORM::CLockObject lock(mutex);

  std::map<uint16_t, Packet>::iterator it = packets.find(pid);
  if (it != packets.end())
    it->second.streaming = true;
}

void AVContext::StopStreaming(uint16_t pid)
{
  PLATFORM::CLockObject lock(mutex);

  std::map<uint16_t, Packet>::iterator it = packets.find(pid);
  if (it != packets.end())
    it->second.streaming = false;
}

ElementaryStream* AVContext::GetStream(uint16_t pid) const
{
  PLATFORM::CLockObject lock(mutex);

  std::map<uint16_t, Packet>::const_iterator it = packets.find(pid);
  if (it != packets.end())
    return it->second.stream;
  return NULL;
}

uint16_t AVContext::GetChannel(uint16_t pid) const
{
  PLATFORM::CLockObject lock(mutex);

  std::map<uint16_t, Packet>::const_iterator it = packets.find(pid);
  if (it != packets.end())
    return it->second.channel;
  return 0xffff;
}

void AVContext::ResetPackets()
{
  PLATFORM::CLockObject lock(mutex);

  for (std::map<uint16_t, Packet>::iterator it = packets.begin(); it != packets.end(); ++it)
  {
    it->second.Reset();
  }
}

////////////////////////////////////////////////////////////////////////////////
/////
/////  MPEG-TS parser for the context
/////

uint8_t AVContext::av_rb8(const unsigned char* p)
{
  uint8_t val = *(uint8_t*)p;
  return val;
}

uint16_t AVContext::av_rb16(const unsigned char* p)
{
  uint16_t val = av_rb8(p) << 8;
  val |= av_rb8(p + 1);
  return val;
}

uint32_t AVContext::av_rb32(const unsigned char* p)
{
  uint32_t val = av_rb16(p) << 16;
  val |= av_rb16(p + 2);
  return val;
}

uint64_t AVContext::decode_pts(const unsigned char* p)
{
  uint64_t pts = (uint64_t)(av_rb8(p) & 0x0e) << 29 | (av_rb16(p + 1) >> 1) << 15 | av_rb16(p + 3) >> 1;
  return pts;
}

STREAM_TYPE AVContext::get_stream_type(uint8_t pes_type)
{
  switch (pes_type)
  {
    case 0x01:
      return STREAM_TYPE_VIDEO_MPEG1;
    case 0x02:
      return STREAM_TYPE_VIDEO_MPEG2;
    case 0x03:
      return STREAM_TYPE_AUDIO_MPEG1;
    case 0x04:
      return STREAM_TYPE_AUDIO_MPEG2;
    case 0x06:
      return STREAM_TYPE_PRIVATE_DATA;
    case 0x0f:
    case 0x11:
      return STREAM_TYPE_AUDIO_AAC;
    case 0x10:
      return STREAM_TYPE_VIDEO_MPEG4;
    case 0x1b:
      return STREAM_TYPE_VIDEO_H264;
    case 0x24:
      return STREAM_TYPE_VIDEO_HEVC;
    case 0xea:
      return STREAM_TYPE_VIDEO_VC1;
    case 0x80:
      return STREAM_TYPE_AUDIO_LPCM;
    case 0x81:
    case 0x83:
    case 0x84:
    case 0x87:
      return STREAM_TYPE_AUDIO_AC3;
    case 0x82:
    case 0x85:
    case 0x8a:
      return STREAM_TYPE_AUDIO_DTS;
  }
  return STREAM_TYPE_UNKNOWN;
}

int AVContext::configure_ts()
{
  uint64_t pos = av_pos;
  int fluts[][2] = {
    {FLUTS_NORMAL_TS_PACKETSIZE, 0},
    {FLUTS_M2TS_TS_PACKETSIZE, 0},
    {FLUTS_DVB_ASI_TS_PACKETSIZE, 0},
    {FLUTS_ATSC_TS_PACKETSIZE, 0}
  };

  uint8_t data[AV_CONTEXT_PACKETSIZE];
  size_t data_size(0);

  int nb = sizeof (fluts) / (2 * sizeof (int));
  int score = TS_CHECK_MIN_SCORE;

  for (int i = 0; i < MAX_RESYNC_SIZE; i++)
  {
    if (!data_size)
      data_size = m_demux->ReadAV(pos, data, AV_CONTEXT_PACKETSIZE) ? AV_CONTEXT_PACKETSIZE : 0;

    if (!data_size)
      return AVCONTEXT_IO_ERROR;

    if (data[AV_CONTEXT_PACKETSIZE - data_size] == 0x47)
    {
      int count, found;
      for (int t = 0; t < nb; t++) // for all fluts
      {
        unsigned char ndata;
        uint64_t npos = pos;
        int do_retry = score; // Reach for score
        do
        {
          --do_retry;
          npos += fluts[t][0];
          if (!m_demux->ReadAV(npos, &ndata, 1))
            return AVCONTEXT_IO_ERROR;
        }
        while (ndata == 0x47 && (++fluts[t][1]) && do_retry);
      }
      // Is score reached ?
      count = found = 0;
      for (int t = 0; t < nb; t++)
      {
        if (fluts[t][1] == score)
        {
          found = t;
          ++count;
        }
        // Reset score for next retry
        fluts[t][1] = 0;
      }
      // One and only one is eligible
      if (count == 1)
      {
        DBG(DEMUX_DBG_DEBUG, "%s: packet size is %d\n", __FUNCTION__, fluts[found][0]);
        av_pkt_size = fluts[found][0];
        av_pos = pos;
        return AVCONTEXT_CONTINUE;
      }
      // More one: Retry for highest score
      else if (count > 1 && ++score > TS_CHECK_MAX_SCORE)
        // Packet size remains undetermined
        break;
      // None: Bad sync. Shift and retry
      else
      {
        --data_size;
        pos++;
      }
    }
    else
    {
      --data_size;
      pos++;
    }
  }

  DBG(DEMUX_DBG_ERROR, "%s: invalid stream\n", __FUNCTION__);
  return AVCONTEXT_TS_NOSYNC;
}

int AVContext::TSResync()
{
  if (!is_configured)
  {
    int ret = configure_ts();
    if (ret != AVCONTEXT_CONTINUE)
      return ret;
    is_configured = true;
  }

  size_t data_size(0);

  for (int i = 0; i < MAX_RESYNC_SIZE; i++)
  {
    if (!data_size)
      data_size = m_demux->ReadAV(av_pos, av_buf, av_pkt_size) ? av_pkt_size : 0;

    if (!data_size)
      return AVCONTEXT_IO_ERROR;

    if (av_buf[av_pkt_size - data_size] == 0x47)
    {
      if (data_size != av_pkt_size)
        data_size = m_demux->ReadAV(av_pos, av_buf, av_pkt_size) ? av_pkt_size : 0;

      if (data_size)
      {
        Reset();
        return AVCONTEXT_CONTINUE;
      }
    }
    av_pos++;
    --data_size;
  }

  return AVCONTEXT_TS_NOSYNC;
}

uint64_t AVContext::GoNext()
{
  av_pos += av_pkt_size;
  Reset();
  return av_pos;
}

uint64_t AVContext::Shift()
{
  av_pos++;
  Reset();
  return av_pos;
}

void AVContext::GoPosition(uint64_t pos, bool rp)
{
  av_pos = pos;
  Reset();
  if(rp)
    for (std::map<uint16_t, Packet>::iterator it = this->packets.begin(); it != this->packets.end(); ++it)
      it->second.Reset();
}

uint64_t AVContext::GetPosition() const
{
  return av_pos;
}

uint64_t AVContext::GetNextPosition() const
{
  return av_pos + av_pkt_size;
}

/*
 * Process TS packet
 *
 * returns:
 *
 * AVCONTEXT_CONTINUE
 *   Parse completed. If has payload, process it else Continue to next packet.
 *
 * AVCONTEXT_STREAM_PID_DATA
 *   Parse completed. A new PES unit starts and data of elementary stream for
 *   the PID must be picked before processing this payload.
 *
 * AVCONTEXT_DISCONTINUITY
 *   Discontinuity. PID will wait until next unit start. So continue to next
 *   packet.
 *
 * AVCONTEXT_TS_NOSYNC
 *   Bad sync byte. Should run TSResync().
 *
 * AVCONTEXT_TS_ERROR
 *  Parsing error !
 */
int AVContext::ProcessTSPacket()
{
  PLATFORM::CLockObject lock(mutex);

  int ret = AVCONTEXT_CONTINUE;
  std::map<uint16_t, Packet>::iterator it;

  if (av_rb8(this->av_buf) != 0x47) // ts sync byte
    return AVCONTEXT_TS_NOSYNC;

  uint16_t header = av_rb16(this->av_buf + 1);
  this->pid = header & 0x1fff;
  this->transport_error = (header & 0x8000) != 0;
  this->payload_unit_start = (header & 0x4000) != 0;
  // Cleaning context
  this->discontinuity = false;
  this->has_payload = false;
  this->payload = NULL;
  this->payload_len = 0;

  if (this->transport_error)
    return AVCONTEXT_CONTINUE;
  // Null packet
  if (this->pid == 0x1fff)
    return AVCONTEXT_CONTINUE;

  uint8_t flags = av_rb8(this->av_buf + 3);
  bool has_payload = (flags & 0x10) != 0;
  bool is_discontinuity = false;
  uint8_t continuity_counter = flags & 0x0f;
  bool has_adaptation = (flags & 0x20) != 0;
  size_t n = 0;
  if (has_adaptation)
  {
    size_t len = (size_t)av_rb8(this->av_buf + 4);
    if (len > (this->av_data_len - 5))
    {
#if defined(TSDEMUX_DEBUG)
      assert(false);
#else
      return AVCONTEXT_TS_ERROR;
#endif
    }
    n = len + 1;
    if (len > 0)
    {
      is_discontinuity = (av_rb8(this->av_buf + 5) & 0x80) != 0;
    }
  }
  if (has_payload)
  {
    // Payload start after adaptation fields
    this->payload = this->av_buf + n + 4;
    this->payload_len = this->av_data_len - n - 4;
  }

  it = this->packets.find(this->pid);
  if (it == this->packets.end())
  {
    // Not registred PID
    // We are waiting for unit start of PID 0 else next packet is required
    if (this->pid == 0 && this->payload_unit_start)
    {
      // Registering PID 0
      Packet pid0;
      pid0.pid = this->pid;
      pid0.packet_type = PACKET_TYPE_PSI;
      pid0.continuity = continuity_counter;
      it = this->packets.insert(it, std::make_pair(this->pid, pid0));
    }
    else
      return AVCONTEXT_CONTINUE;
  }
  else
  {
    // PID is registred
    // Checking unit start is required
    if (it->second.wait_unit_start && !this->payload_unit_start)
    {
      // Not unit start. Save packet flow continuity...
      it->second.continuity = continuity_counter;
      this->discontinuity = true;
      return AVCONTEXT_DISCONTINUITY;
    }
    // Checking continuity where possible
    if (it->second.continuity != 0xff)
    {
      uint8_t expected_cc = has_payload ? (it->second.continuity + 1) & 0x0f : it->second.continuity;
      if (!is_discontinuity && expected_cc != continuity_counter)
      {
        this->discontinuity = true;
        // If unit is not start then reset PID and wait the next unit start
        if (!this->payload_unit_start)
        {
          it->second.Reset();
          DBG(DEMUX_DBG_WARN, "PID %.4x discontinuity detected: found %u, expected %u\n", this->pid, continuity_counter, expected_cc);
          return AVCONTEXT_DISCONTINUITY;
        }
      }
    }
    it->second.continuity = continuity_counter;
  }

  this->discontinuity |= is_discontinuity;
  this->has_payload = has_payload;
  this->packet = &(it->second);

  // It is time to stream data for PES
  if (this->payload_unit_start &&
          this->packet->streaming &&
          this->packet->packet_type == PACKET_TYPE_PES &&
          !this->packet->wait_unit_start)
  {
    this->packet->has_stream_data = true;
    ret = AVCONTEXT_STREAM_PID_DATA;
    payload_unit_pos = prev_payload_unit_pos;
    prev_payload_unit_pos = av_pos;
  }
  return ret;
}

/*
 * Process payload of packet depending of its type
 *
 * PACKET_TYPE_PSI -> parse_ts_psi()
 * PACKET_TYPE_PES -> parse_ts_pes()
 */
int AVContext::ProcessTSPayload()
{
  PLATFORM::CLockObject lock(mutex);

  if (!this->packet)
    return AVCONTEXT_CONTINUE;

  int ret = 0;
  switch (this->packet->packet_type)
  {
    case PACKET_TYPE_PSI:
      ret = parse_ts_psi();
      break;
    case PACKET_TYPE_PES:
      ret = parse_ts_pes();
      break;
    case PACKET_TYPE_UNKNOWN:
      break;
  }

  return ret;
}

void AVContext::clear_pmt()
{
  DBG(DEMUX_DBG_DEBUG, "%s\n", __FUNCTION__);
  std::vector<uint16_t> pid_list;
  for (std::map<uint16_t, Packet>::iterator it = this->packets.begin(); it != this->packets.end(); ++it)
  {
    if (it->second.packet_type == PACKET_TYPE_PSI && it->second.packet_table.table_id == 0x02)
    {
      pid_list.push_back(it->first);
      clear_pes(it->second.channel);
    }
  }
  for (std::vector<uint16_t>::iterator it = pid_list.begin(); it != pid_list.end(); ++it)
    this->packets.erase(*it);
}

void AVContext::clear_pes(uint16_t channel)
{
  DBG(DEMUX_DBG_DEBUG, "%s(%u)\n", __FUNCTION__, channel);
  std::vector<uint16_t> pid_list;
  for (std::map<uint16_t, Packet>::iterator it = this->packets.begin(); it != this->packets.end(); ++it)
  {
    if (it->second.packet_type == PACKET_TYPE_PES && it->second.channel == channel)
      pid_list.push_back(it->first);
  }
  for (std::vector<uint16_t>::iterator it = pid_list.begin(); it != pid_list.end(); ++it)
    this->packets.erase(*it);
}

/*
 * Parse PSI payload
 *
 * returns:
 *
 * AVCONTEXT_CONTINUE
 *   Parse completed. Continue to next packet
 *
 * AVCONTEXT_PROGRAM_CHANGE
 *   Parse completed. The program has changed. All streams are resetted and
 *   streaming flag is set to false. Client must inspect streams MAP and enable
 *   streaming for those recognized.
 *
 * AVCONTEXT_TS_ERROR
 *  Parsing error !
 */
int AVContext::parse_ts_psi()
{
  size_t len;

  if (!this->has_payload || !this->payload || !this->payload_len || !this->packet)
    return AVCONTEXT_CONTINUE;

  if (this->payload_unit_start)
  {
    // Reset wait for unit start
    this->packet->wait_unit_start = false;
    // pointer field present
    len = (size_t)av_rb8(this->payload);
    if (len > this->payload_len)
    {
#if defined(TSDEMUX_DEBUG)
      assert(false);
#else
      return AVCONTEXT_TS_ERROR;
#endif
    }

    // table ID
    uint8_t table_id = av_rb8(this->payload + 1);

    // table length
    len = (size_t)av_rb16(this->payload + 2);
    if ((len & 0x3000) != 0x3000)
    {
#if defined(TSDEMUX_DEBUG)
      assert(false);
#else
      return AVCONTEXT_TS_ERROR;
#endif
    }
    len &= 0x0fff;

    this->packet->packet_table.Reset();

    size_t n = this->payload_len - 4;
    memcpy(this->packet->packet_table.buf, this->payload + 4, n);
    this->packet->packet_table.table_id = table_id;
    this->packet->packet_table.offset = n;
    this->packet->packet_table.len = len;
    // check for incomplete section
    if (this->packet->packet_table.offset < this->packet->packet_table.len)
      return AVCONTEXT_CONTINUE;
  }
  else
  {
    // next part of PSI
    if (this->packet->packet_table.offset == 0)
    {
#if defined(TSDEMUX_DEBUG)
      assert(false);
#else
      return AVCONTEXT_TS_ERROR;
#endif
    }

    if ((this->payload_len + this->packet->packet_table.offset) > TABLE_BUFFER_SIZE)
    {
#if defined(TSDEMUX_DEBUG)
      assert(false);
#else
      return AVCONTEXT_TS_ERROR;
#endif
    }
    memcpy(this->packet->packet_table.buf + this->packet->packet_table.offset, this->payload, this->payload_len);
    this->packet->packet_table.offset += this->payload_len;
    // check for incomplete section
    if (this->packet->packet_table.offset < this->packet->packet_table.len)
      return AVCONTEXT_CONTINUE;
  }

  // now entire table is filled
  const unsigned char* psi = this->packet->packet_table.buf;
  const unsigned char* end_psi = psi + this->packet->packet_table.len;

  switch (this->packet->packet_table.table_id)
  {
    case 0x00: // parse PAT table
    {
      // check if version number changed
      uint16_t id = av_rb16(psi);
      // check if applicable
      if ((av_rb8(psi + 2) & 0x01) == 0)
        return AVCONTEXT_CONTINUE;
      // check if version number changed
      uint8_t version = (av_rb8(psi + 2) & 0x3e) >> 1;
      if (id == this->packet->packet_table.id && version == this->packet->packet_table.version)
        return AVCONTEXT_CONTINUE;
      DBG(DEMUX_DBG_DEBUG, "%s: new PAT version %u\n", __FUNCTION__, version);

      // clear old associated pmt
      clear_pmt();

      // parse new version of PAT
      psi += 5;

      end_psi -= 4; // CRC32

      if (psi >= end_psi)
      {
#if defined(TSDEMUX_DEBUG)
        assert(false);
#else
        return AVCONTEXT_TS_ERROR;
#endif
      }

      len = end_psi - psi;

      if (len % 4)
      {
#if defined(TSDEMUX_DEBUG)
        assert(false);
#else
        return AVCONTEXT_TS_ERROR;
#endif
      }

      size_t n = len / 4;

      for (size_t i = 0; i < n; i++, psi += 4)
      {
        uint16_t channel = av_rb16(psi);
        uint16_t pmt_pid = av_rb16(psi + 2);

        // Reserved fields in table sections must be "set" to '1' bits.
        //if ((pmt_pid & 0xe000) != 0xe000)
        //  return AVCONTEXT_TS_ERROR;

        pmt_pid &= 0x1fff;

        DBG(DEMUX_DBG_DEBUG, "%s: PAT version %u: new PMT %.4x channel %u\n", __FUNCTION__, version, pmt_pid, channel);
        if (this->channel == 0 || this->channel == channel)
        {
          Packet& pmt = this->packets[pmt_pid];
          pmt.pid = pmt_pid;
          pmt.packet_type = PACKET_TYPE_PSI;
          pmt.channel = channel;
          DBG(DEMUX_DBG_DEBUG, "%s: PAT version %u: register PMT %.4x channel %u\n", __FUNCTION__, version, pmt_pid, channel);
        }
      }
      // PAT is processed. New version is available
      this->packet->packet_table.id = id;
      this->packet->packet_table.version = version;
      break;
    }
    case 0x02: // parse PMT table
    {
      uint16_t id = av_rb16(psi);
      // check if applicable
      if ((av_rb8(psi + 2) & 0x01) == 0)
        return AVCONTEXT_CONTINUE;
      // check if version number changed
      uint8_t version = (av_rb8(psi + 2) & 0x3e) >> 1;
      if (id == this->packet->packet_table.id && version == this->packet->packet_table.version)
        return AVCONTEXT_CONTINUE;
      DBG(DEMUX_DBG_DEBUG, "%s: PMT(%.4x) version %u\n", __FUNCTION__, this->packet->pid, version);

      // clear old pes
      clear_pes(this->packet->channel);

      // parse new version of PMT
      psi += 7;

      end_psi -= 4; // CRC32

      if (psi >= end_psi)
      {
#if defined(TSDEMUX_DEBUG)
        assert(false);
#else
        return AVCONTEXT_TS_ERROR;
#endif
      }

      len = (size_t)(av_rb16(psi) & 0x0fff);
      psi += 2 + len;

      while (psi < end_psi)
      {
        if (end_psi - psi < 5)
        {
#if defined(TSDEMUX_DEBUG)
        assert(false);
#else
        return AVCONTEXT_TS_ERROR;
#endif
        }

        uint8_t pes_type = av_rb8(psi);
        uint16_t pes_pid = av_rb16(psi + 1);

        // Reserved fields in table sections must be "set" to '1' bits.
        //if ((pes_pid & 0xe000) != 0xe000)
        //  return AVCONTEXT_TS_ERROR;

        pes_pid &= 0x1fff;

        // len of descriptor section
        len = (size_t)(av_rb16(psi + 3) & 0x0fff);
        psi += 5;

        // ignore unknown streams
        STREAM_TYPE stream_type = get_stream_type(pes_type);
        DBG(DEMUX_DBG_DEBUG, "%s: PMT(%.4x) version %u: new PES %.4x %s\n", __FUNCTION__,
                  this->packet->pid, version, pes_pid, ElementaryStream::GetStreamCodecName(stream_type));
        if (stream_type != STREAM_TYPE_UNKNOWN)
        {
          Packet& pes = this->packets[pes_pid];
          pes.pid = pes_pid;
          pes.packet_type = PACKET_TYPE_PES;
          pes.channel = this->packet->channel;
          // Disable streaming by default
          pes.streaming = false;
          // Get basic stream infos from PMT table
          STREAM_INFO stream_info;
          stream_info = parse_pes_descriptor(psi, len, &stream_type);

          ElementaryStream* es;
          switch (stream_type)
          {
          case STREAM_TYPE_VIDEO_MPEG1:
          case STREAM_TYPE_VIDEO_MPEG2:
            es = new ES_MPEG2Video(pes_pid);
            break;
          case STREAM_TYPE_AUDIO_MPEG1:
          case STREAM_TYPE_AUDIO_MPEG2:
            es = new ES_MPEG2Audio(pes_pid);
            break;
          case STREAM_TYPE_AUDIO_AAC:
          case STREAM_TYPE_AUDIO_AAC_ADTS:
          case STREAM_TYPE_AUDIO_AAC_LATM:
            es = new ES_AAC(pes_pid);
            break;
          case STREAM_TYPE_VIDEO_H264:
            es = new ES_h264(pes_pid);
            break;
          case STREAM_TYPE_VIDEO_HEVC:
            es = new ES_hevc(pes_pid);
            break;
          case STREAM_TYPE_AUDIO_AC3:
          case STREAM_TYPE_AUDIO_EAC3:
            es = new ES_AC3(pes_pid);
            break;
          case STREAM_TYPE_DVB_SUBTITLE:
            es = new ES_Subtitle(pes_pid);
            break;
          case STREAM_TYPE_DVB_TELETEXT:
            es = new ES_Teletext(pes_pid);
            break;
          default:
            // No parser: pass-through
            es = new ElementaryStream(pes_pid);
            es->has_stream_info = true;
            break;
          }

          es->stream_type = stream_type;
          es->stream_info = stream_info;
          pes.stream = es;
          DBG(DEMUX_DBG_DEBUG, "%s: PMT(%.4x) version %u: register PES %.4x %s\n", __FUNCTION__,
                  this->packet->pid, version, pes_pid, es->GetStreamCodecName());
        }
        psi += len;
      }

      if (psi != end_psi)
      {
#if defined(TSDEMUX_DEBUG)
        assert(false);
#else
        return AVCONTEXT_TS_ERROR;
#endif
      }

      // PMT is processed. New version is available
      this->packet->packet_table.id = id;
      this->packet->packet_table.version = version;
      return AVCONTEXT_PROGRAM_CHANGE;
    }
    default:
      // CAT, NIT table
      break;
  }

  return AVCONTEXT_CONTINUE;
}

STREAM_INFO AVContext::parse_pes_descriptor(const unsigned char* p, size_t len, STREAM_TYPE* st)
{
  const unsigned char* desc_end = p + len;
  STREAM_INFO si;
  memset(&si, 0, sizeof(STREAM_INFO));

  while (p < desc_end)
  {
    uint8_t desc_tag = av_rb8(p);
    uint8_t desc_len = av_rb8(p + 1);
    p += 2;
    DBG(DEMUX_DBG_DEBUG, "%s: tag %.2x len %d\n", __FUNCTION__, desc_tag, desc_len);
    switch (desc_tag)
    {
      case 0x02:
      case 0x03:
        break;
      case 0x0a: /* ISO 639 language descriptor */
        if (desc_len >= 4)
        {
          si.language[0] = av_rb8(p);
          si.language[1] = av_rb8(p + 1);
          si.language[2] = av_rb8(p + 2);
          si.language[3] = 0;
        }
        break;
      case 0x56: /* DVB teletext descriptor */
        *st = STREAM_TYPE_DVB_TELETEXT;
        break;
      case 0x6a: /* DVB AC3 */
      case 0x81: /* AC3 audio stream */
        *st = STREAM_TYPE_AUDIO_AC3;
        break;
      case 0x7a: /* DVB enhanced AC3 */
        *st = STREAM_TYPE_AUDIO_EAC3;
        break;
      case 0x7b: /* DVB DTS */
        *st = STREAM_TYPE_AUDIO_DTS;
        break;
      case 0x7c: /* DVB AAC */
        *st = STREAM_TYPE_AUDIO_AAC;
        break;
      case 0x59: /* subtitling descriptor */
        if (desc_len >= 8)
        {
          /*
           * Byte 4 is the subtitling_type field
           * av_rb8(p + 3) & 0x10 : normal
           * av_rb8(p + 3) & 0x20 : for the hard of hearing
           */
          *st = STREAM_TYPE_DVB_SUBTITLE;
          si.language[0] = av_rb8(p);
          si.language[1] = av_rb8(p + 1);
          si.language[2] = av_rb8(p + 2);
          si.language[3] = 0;
          si.composition_id = (int)av_rb16(p + 4);
          si.ancillary_id = (int)av_rb16(p + 6);
        }
        break;
      case 0x05: /* registration descriptor */
      case 0x1E: /* SL descriptor */
      case 0x1F: /* FMC descriptor */
      case 0x52: /* stream identifier descriptor */
    default:
      break;
    }
    p += desc_len;
  }

  return si;
}

/*
 * Parse PES payload
 *
 * returns:
 *
 * AVCONTEXT_CONTINUE
 *   Parse completed. When streaming is enabled for PID, data is appended to
 *   the frame buffer of corresponding elementary stream.
 *
 * AVCONTEXT_TS_ERROR
 *  Parsing error !
 */
int AVContext::parse_ts_pes()
{
  if (!this->has_payload || !this->payload || !this->payload_len || !this->packet)
    return AVCONTEXT_CONTINUE;

  if (!this->packet->stream)
    return AVCONTEXT_CONTINUE;

  if (this->payload_unit_start)
  {
    // Wait for unit start: Reset frame buffer to clear old data
    if (this->packet->wait_unit_start)
    {
      packet->stream->Reset();
      packet->stream->p_dts = PTS_UNSET;
      packet->stream->p_pts = PTS_UNSET;
    }
    this->packet->wait_unit_start = false;
    this->packet->has_stream_data = false;
    // Reset header table
    this->packet->packet_table.Reset();
    // Header len is at least 6 bytes. So getting 6 bytes first
    this->packet->packet_table.len = 6;
  }

  // Position in the payload buffer. Start at 0
  size_t pos = 0;

  while (this->packet->packet_table.offset < this->packet->packet_table.len)
  {
    if (pos >= this->payload_len)
      return AVCONTEXT_CONTINUE;

    size_t n = this->packet->packet_table.len - this->packet->packet_table.offset;

    if (n > (this->payload_len - pos))
      n = this->payload_len - pos;

    memcpy(this->packet->packet_table.buf + this->packet->packet_table.offset, this->payload + pos, n);
    this->packet->packet_table.offset += n;
    pos += n;

    if (this->packet->packet_table.offset == 6)
    {
      if (memcmp(this->packet->packet_table.buf, "\x00\x00\x01", 3) == 0)
      {
        uint8_t stream_id = av_rb8(this->packet->packet_table.buf + 3);
        if (stream_id == 0xbd || (stream_id >= 0xc0 && stream_id <= 0xef))
          this->packet->packet_table.len = 9;
      }
    }
    else if (this->packet->packet_table.offset == 9)
    {
      this->packet->packet_table.len += av_rb8(this->packet->packet_table.buf + 8);
    }
  }

  // parse header table
  bool has_pts = false;

  if (this->packet->packet_table.len >= 9)
  {
    uint8_t flags = av_rb8(this->packet->packet_table.buf + 7);

    //this->packet->stream->frame_num++;

    switch (flags & 0xc0)
    {
      case 0x80: // PTS only
      {
        has_pts = true;
        if (this->packet->packet_table.len >= 14)
        {
          int64_t pts = decode_pts(this->packet->packet_table.buf + 9);
          this->packet->stream->p_dts = this->packet->stream->c_dts;
          this->packet->stream->p_pts = this->packet->stream->c_pts;
          this->packet->stream->c_dts = this->packet->stream->c_pts = pts;
        }
        else
        {
          this->packet->stream->c_dts = this->packet->stream->c_pts = PTS_UNSET;
        }
      }
      break;
      case 0xc0: // PTS,DTS
      {
        has_pts = true;
        if (this->packet->packet_table.len >= 19 )
        {
          int64_t pts = decode_pts(this->packet->packet_table.buf + 9);
          int64_t dts = decode_pts(this->packet->packet_table.buf + 14);

          int64_t d(0);
          if (pts < dts)
            dts = PTS_UNSET;
          else
            d = (pts - dts) & PTS_MASK;

          // more than two seconds of PTS/DTS delta, probably corrupt
          if(d > 180000)
          {
            this->packet->stream->c_dts = this->packet->stream->c_pts = PTS_UNSET;
          }
          else
          {
            this->packet->stream->p_dts = this->packet->stream->c_dts;
            this->packet->stream->p_pts = this->packet->stream->c_pts;
            this->packet->stream->c_dts = dts;
            this->packet->stream->c_pts = pts;
          }
        }
        else
        {
          this->packet->stream->c_dts = this->packet->stream->c_pts = PTS_UNSET;
        }
      }
      break;
    }
    this->packet->packet_table.Reset();
  }

  if (this->packet->streaming)
  {
    const unsigned char* data = this->payload + pos;
    size_t len = this->payload_len - pos;
    this->packet->stream->Append(data, len, has_pts);
  }

  return AVCONTEXT_CONTINUE;
}
