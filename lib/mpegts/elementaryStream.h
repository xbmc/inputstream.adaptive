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

#ifndef ELEMENTARYSTREAM_H
#define ELEMENTARYSTREAM_H

#include <inttypes.h>
#include <cstddef>    // for size_t

#define ES_INIT_BUFFER_SIZE     64000
#define ES_MAX_BUFFER_SIZE      1048576
#define PTS_MASK                0x1ffffffffLL
#define PTS_UNSET               0x1ffffffffLL
#define PTS_TIME_BASE           90000LL
#define RESCALE_TIME_BASE       1000000LL

namespace TSDemux
{
  enum STREAM_TYPE
  {
    STREAM_TYPE_UNKNOWN = 0,
    STREAM_TYPE_VIDEO_MPEG1,
    STREAM_TYPE_VIDEO_MPEG2,
    STREAM_TYPE_AUDIO_MPEG1,
    STREAM_TYPE_AUDIO_MPEG2,
    STREAM_TYPE_AUDIO_AAC,
    STREAM_TYPE_AUDIO_AAC_ADTS,
    STREAM_TYPE_AUDIO_AAC_LATM,
    STREAM_TYPE_VIDEO_H264,
    STREAM_TYPE_VIDEO_HEVC,
    STREAM_TYPE_AUDIO_AC3,
    STREAM_TYPE_AUDIO_EAC3,
    STREAM_TYPE_DVB_TELETEXT,
    STREAM_TYPE_DVB_SUBTITLE,
    STREAM_TYPE_VIDEO_MPEG4,
    STREAM_TYPE_VIDEO_VC1,
    STREAM_TYPE_AUDIO_LPCM,
    STREAM_TYPE_AUDIO_DTS,
    STREAM_TYPE_PRIVATE_DATA
  };

  struct STREAM_INFO
  {
    char                  language[4];
    int                   composition_id;
    int                   ancillary_id;
    int                   fps_scale;
    int                   fps_rate;
    int                   height;
    int                   width;
    float                 aspect;
    int                   channels;
    int                   sample_rate;
    int                   block_align;
    int                   bit_rate;
    int                   bits_per_sample;
    bool                  interlaced;
    uint8_t               extra_data[256];
    int                   extra_data_size;
  };

  struct STREAM_PKT
  {
    uint16_t              pid;
    size_t                size;
    const unsigned char*  data;
    int64_t               dts;
    int64_t               pts;
    uint64_t              duration;
    bool                  streamChange;
    bool                  recoveryPoint;
  };

  class ElementaryStream
  {
  public:
    ElementaryStream(uint16_t pes_pid);
    virtual ~ElementaryStream();
    virtual void Reset();
    void ClearBuffer();
    int Append(const unsigned char* buf, size_t len, bool new_pts = false);
    const char* GetStreamCodecName() const;
    static const char* GetStreamCodecName(STREAM_TYPE stream_type);

    uint16_t pid;
    STREAM_TYPE stream_type;
    int64_t c_dts;               ///< current MPEG stream DTS (decode time for video)
    int64_t c_pts;               ///< current MPEG stream PTS (presentation time for audio and video)
    int64_t p_dts;               ///< previous MPEG stream DTS (decode time for video)
    int64_t p_pts;               ///< previous MPEG stream PTS (presentation time for audio and video)

    bool has_stream_info;         ///< true if stream info is completed else it requires parsing of iframe

    STREAM_INFO stream_info;

    bool GetStreamPacket(STREAM_PKT* pkt);
    virtual void Parse(STREAM_PKT* pkt);

  protected:
    void ResetStreamPacket(STREAM_PKT* pkt);
    uint64_t Rescale(uint64_t a, uint64_t b, uint64_t c);
    bool SetVideoInformation(int FpsScale, int FpsRate, int Height, int Width, float Aspect, bool Interlaced);
    bool SetAudioInformation(int Channels, int SampleRate, int BitRate, int BitsPerSample, int BlockAlign);

    size_t es_alloc_init;         ///< Initial allocation of memory for buffer
    unsigned char* es_buf;        ///< The Pointer to buffer
    size_t es_alloc;              ///< Allocated size of memory for buffer
    size_t es_len;                ///< Size of data in buffer
    size_t es_consumed;           ///< Consumed payload. Will be erased on next append
    size_t es_pts_pointer;        ///< Position in buffer where current PTS becomes applicable
    size_t es_parsed;             ///< Parser: Last processed position in buffer
    bool   es_found_frame;        ///< Parser: Found frame
    bool   es_frame_valid;
    bool   es_extraDataChanged;
  };
}

#endif /* ELEMENTARYSTREAM_H */
