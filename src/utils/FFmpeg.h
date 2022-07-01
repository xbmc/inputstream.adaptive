/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <stdint.h>

// This implement parts of FFmpeg package
// must be kept up-to-date with the respective includes
// Updated from: https://github.com/xbmc/FFmpeg
// Tag release: 4.4.1-Nexus-Alpha1

namespace UTILS
{
namespace FFMPEG
{

// -------- The code below this line is part of: libavcodec/packet.h --------

/**
 * @defgroup lavc_packet AVPacket
 *
 * Types and functions for working with AVPacket.
 * @{
 */
enum AVPacketSideDataType
{
  /**
   * An AV_PKT_DATA_PALETTE side data packet contains exactly AVPALETTE_SIZE
   * bytes worth of palette. This side data signals that a new palette is
   * present.
   */
  AV_PKT_DATA_PALETTE,

  /**
   * The AV_PKT_DATA_NEW_EXTRADATA is used to notify the codec or the format
   * that the extradata buffer was changed and the receiving side should
   * act upon it appropriately. The new extradata is embedded in the side
   * data buffer and should be immediately used for processing the current
   * frame or packet.
   */
  AV_PKT_DATA_NEW_EXTRADATA,

  /**
   * An AV_PKT_DATA_PARAM_CHANGE side data packet is laid out as follows:
   * @code
   * u32le param_flags
   * if (param_flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_COUNT)
   *     s32le channel_count
   * if (param_flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_LAYOUT)
   *     u64le channel_layout
   * if (param_flags & AV_SIDE_DATA_PARAM_CHANGE_SAMPLE_RATE)
   *     s32le sample_rate
   * if (param_flags & AV_SIDE_DATA_PARAM_CHANGE_DIMENSIONS)
   *     s32le width
   *     s32le height
   * @endcode
   */
  AV_PKT_DATA_PARAM_CHANGE,

  /**
   * An AV_PKT_DATA_H263_MB_INFO side data packet contains a number of
   * structures with info about macroblocks relevant to splitting the
   * packet into smaller packets on macroblock edges (e.g. as for RFC 2190).
   * That is, it does not necessarily contain info about all macroblocks,
   * as long as the distance between macroblocks in the info is smaller
   * than the target payload size.
   * Each MB info structure is 12 bytes, and is laid out as follows:
   * @code
   * u32le bit offset from the start of the packet
   * u8    current quantizer at the start of the macroblock
   * u8    GOB number
   * u16le macroblock address within the GOB
   * u8    horizontal MV predictor
   * u8    vertical MV predictor
   * u8    horizontal MV predictor for block number 3
   * u8    vertical MV predictor for block number 3
   * @endcode
   */
  AV_PKT_DATA_H263_MB_INFO,

  /**
   * This side data should be associated with an audio stream and contains
   * ReplayGain information in form of the AVReplayGain struct.
   */
  AV_PKT_DATA_REPLAYGAIN,

  /**
   * This side data contains a 3x3 transformation matrix describing an affine
   * transformation that needs to be applied to the decoded video frames for
   * correct presentation.
   *
   * See libavutil/display.h for a detailed description of the data.
   */
  AV_PKT_DATA_DISPLAYMATRIX,

  /**
   * This side data should be associated with a video stream and contains
   * Stereoscopic 3D information in form of the AVStereo3D struct.
   */
  AV_PKT_DATA_STEREO3D,

  /**
   * This side data should be associated with an audio stream and corresponds
   * to enum AVAudioServiceType.
   */
  AV_PKT_DATA_AUDIO_SERVICE_TYPE,

  /**
   * This side data contains quality related information from the encoder.
   * @code
   * u32le quality factor of the compressed frame. Allowed range is between 1 (good) and FF_LAMBDA_MAX (bad).
   * u8    picture type
   * u8    error count
   * u16   reserved
   * u64le[error count] sum of squared differences between encoder in and output
   * @endcode
   */
  AV_PKT_DATA_QUALITY_STATS,

  /**
   * This side data contains an integer value representing the stream index
   * of a "fallback" track.  A fallback track indicates an alternate
   * track to use when the current track can not be decoded for some reason.
   * e.g. no decoder available for codec.
   */
  AV_PKT_DATA_FALLBACK_TRACK,

  /**
   * This side data corresponds to the AVCPBProperties struct.
   */
  AV_PKT_DATA_CPB_PROPERTIES,

  /**
   * Recommmends skipping the specified number of samples
   * @code
   * u32le number of samples to skip from start of this packet
   * u32le number of samples to skip from end of this packet
   * u8    reason for start skip
   * u8    reason for end   skip (0=padding silence, 1=convergence)
   * @endcode
   */
  AV_PKT_DATA_SKIP_SAMPLES,

  /**
   * An AV_PKT_DATA_JP_DUALMONO side data packet indicates that
   * the packet may contain "dual mono" audio specific to Japanese DTV
   * and if it is true, recommends only the selected channel to be used.
   * @code
   * u8    selected channels (0=mail/left, 1=sub/right, 2=both)
   * @endcode
   */
  AV_PKT_DATA_JP_DUALMONO,

  /**
   * A list of zero terminated key/value strings. There is no end marker for
   * the list, so it is required to rely on the side data size to stop.
   */
  AV_PKT_DATA_STRINGS_METADATA,

  /**
   * Subtitle event position
   * @code
   * u32le x1
   * u32le y1
   * u32le x2
   * u32le y2
   * @endcode
   */
  AV_PKT_DATA_SUBTITLE_POSITION,

  /**
   * Data found in BlockAdditional element of matroska container. There is
   * no end marker for the data, so it is required to rely on the side data
   * size to recognize the end. 8 byte id (as found in BlockAddId) followed
   * by data.
   */
  AV_PKT_DATA_MATROSKA_BLOCKADDITIONAL,

  /**
   * The optional first identifier line of a WebVTT cue.
   */
  AV_PKT_DATA_WEBVTT_IDENTIFIER,

  /**
   * The optional settings (rendering instructions) that immediately
   * follow the timestamp specifier of a WebVTT cue.
   */
  AV_PKT_DATA_WEBVTT_SETTINGS,

  /**
   * A list of zero terminated key/value strings. There is no end marker for
   * the list, so it is required to rely on the side data size to stop. This
   * side data includes updated metadata which appeared in the stream.
   */
  AV_PKT_DATA_METADATA_UPDATE,

  /**
   * MPEGTS stream ID as uint8_t, this is required to pass the stream ID
   * information from the demuxer to the corresponding muxer.
   */
  AV_PKT_DATA_MPEGTS_STREAM_ID,

  /**
   * Mastering display metadata (based on SMPTE-2086:2014). This metadata
   * should be associated with a video stream and contains data in the form
   * of the AVMasteringDisplayMetadata struct.
   */
  AV_PKT_DATA_MASTERING_DISPLAY_METADATA,

  /**
   * This side data should be associated with a video stream and corresponds
   * to the AVSphericalMapping structure.
   */
  AV_PKT_DATA_SPHERICAL,

  /**
   * Content light level (based on CTA-861.3). This metadata should be
   * associated with a video stream and contains data in the form of the
   * AVContentLightMetadata struct.
   */
  AV_PKT_DATA_CONTENT_LIGHT_LEVEL,

  /**
   * ATSC A53 Part 4 Closed Captions. This metadata should be associated with
   * a video stream. A53 CC bitstream is stored as uint8_t in AVPacketSideData.data.
   * The number of bytes of CC data is AVPacketSideData.size.
   */
  AV_PKT_DATA_A53_CC,

  /**
   * This side data is encryption initialization data.
   * The format is not part of ABI, use av_encryption_init_info_* methods to
   * access.
   */
  AV_PKT_DATA_ENCRYPTION_INIT_INFO,

  /**
   * This side data contains encryption info for how to decrypt the packet.
   * The format is not part of ABI, use av_encryption_info_* methods to access.
   */
  AV_PKT_DATA_ENCRYPTION_INFO,

  /**
   * Active Format Description data consisting of a single byte as specified
   * in ETSI TS 101 154 using AVActiveFormatDescription enum.
   */
  AV_PKT_DATA_AFD,

  /**
   * Producer Reference Time data corresponding to the AVProducerReferenceTime struct,
   * usually exported by some encoders (on demand through the prft flag set in the
   * AVCodecContext export_side_data field).
   */
  AV_PKT_DATA_PRFT,

  /**
   * ICC profile data consisting of an opaque octet buffer following the
   * format described by ISO 15076-1.
   */
  AV_PKT_DATA_ICC_PROFILE,

  /**
   * DOVI configuration
   * ref:
   * dolby-vision-bitstreams-within-the-iso-base-media-file-format-v2.1.2, section 2.2
   * dolby-vision-bitstreams-in-mpeg-2-transport-stream-multiplex-v1.2, section 3.3
   * Tags are stored in struct AVDOVIDecoderConfigurationRecord.
   */
  AV_PKT_DATA_DOVI_CONF,

  /**
   * Timecode which conforms to SMPTE ST 12-1:2014. The data is an array of 4 uint32_t
   * where the first uint32_t describes how many (1-3) of the other timecodes are used.
   * The timecode format is described in the documentation of av_timecode_get_smpte_from_framenum()
   * function in libavutil/timecode.h.
   */
  AV_PKT_DATA_S12M_TIMECODE,

  /**
   * The number of side data types.
   * This is not part of the public API/ABI in the sense that it may
   * change when new side data types are added.
   * This must stay the last enum value.
   * If its value becomes huge, some code using it
   * needs to be updated as it assumes it to be smaller than other limits.
   */
  AV_PKT_DATA_NB
};

typedef struct AVPacketSideData
{
  uint8_t* data;
#if HAVE_FFMPEG_5 // Enable when kodi will be built with FFmpeg >= v5.0
  size_t size;
#else
  int size;
#endif
  enum AVPacketSideDataType type;
} AVPacketSideData;

} // namespace FFMPEG
} // namespace UTILS
