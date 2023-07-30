/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <array>
#include <cstdint>
#include <ctime> // time
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace UTILS
{

std::string AnnexbToHvcc(const char* b16Data);
std::string AnnexbToAvc(const char* b16Data);
std::string AvcToAnnexb(const std::string& avc);
std::string ConvertKIDtoWVKID(std::string_view kid);
std::string ConvertKIDtoUUID(std::string_view kid);
bool CreateISMlicense(std::string_view key,
                      std::string_view licenseData,
                      std::vector<uint8_t>& initData);
void ParseHeaderString(std::map<std::string, std::string>& headerMap, const std::string& header);

/*!
 * \brief Get the current timestamp
 * \return The timestamp in milliseconds
 */
uint64_t GetTimestamp();

namespace CODEC
{
constexpr char* NAME_UNKNOWN = "unk"; // Kodi codec name for unknown codec

// IMPORTANT: Codec names must match the ffmpeg library definition
// https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/codec_desc.c

// Video definitions

constexpr char* NAME_MPEG1 = "mpeg1video";
constexpr char* NAME_MPEG2 = "mpeg2video";
constexpr char* NAME_MPEG4 = "mpeg4"; // MPEG-4 part 2
constexpr char* NAME_VC1 = "vc1"; // SMPTE VC-1
constexpr char* NAME_H264 = "h264"; // MPEG-4 AVC
constexpr char* NAME_HEVC = "hevc";
constexpr char* NAME_VP9 = "vp9";
constexpr char* NAME_AV1 = "av1";

constexpr std::array VIDEO_NAME_LIST = {NAME_MPEG1, NAME_MPEG2, NAME_MPEG4, NAME_VC1,
                                        NAME_H264,  NAME_HEVC,  NAME_VP9,   NAME_AV1};

// Audio definitions

constexpr char* NAME_AAC = "aac";
constexpr char* NAME_DTS = "dts";
constexpr char* NAME_AC3 = "ac3";
constexpr char* NAME_EAC3 = "eac3"; // Enhanced AC-3
constexpr char* NAME_OPUS = "opus";
constexpr char* NAME_VORBIS = "vorbis";

constexpr std::array AUDIO_NAME_LIST = {NAME_AAC,  NAME_DTS,  NAME_AC3,
                                        NAME_EAC3, NAME_OPUS, NAME_VORBIS};

// Subtitles definitions

constexpr char* NAME_SRT = "srt";
constexpr char* NAME_WEBVTT = "webvtt";
constexpr char* NAME_TTML = "ttml";

// Fourcc video definitions

constexpr char* FOURCC_H264 = "h264"; // MPEG-4 AVC
constexpr char* FOURCC_AVC_ = "avc"; // Generic prefix for all avc* fourcc, e.g. avc1, avcb
constexpr char* FOURCC_VP09 = "vp09"; // VP9
constexpr char* FOURCC_AV01 = "av01"; // AV1
constexpr char* FOURCC_HEVC = "hevc";
constexpr char* FOURCC_HVC1 = "hvc1"; // HEVC Dolby vision
constexpr char* FOURCC_DVH1 = "dvh1"; // HEVC Dolby vision, hvc1 variant
constexpr char* FOURCC_HEV1 = "hev1"; // HEVC Dolby vision
constexpr char* FOURCC_DVHE = "dvhe"; // HEVC Dolby vision, hev1 variant

constexpr std::array VIDEO_FOURCC_LIST = {FOURCC_H264,   FOURCC_AVC_,  FOURCC_VP09,   FOURCC_AV01,
                                          FOURCC_HEVC,   FOURCC_HVC1,  FOURCC_DVH1,   FOURCC_HEV1,
                                          FOURCC_DVHE};

// Fourcc audio definitions

constexpr char* FOURCC_MP4A = "mp4a";
constexpr char* FOURCC_AAC_ = "aac"; // Generic prefix for all aac* fourcc, e.g. aac, aacl, aacp
constexpr char* FOURCC_AACL = "aacl";
constexpr char* FOURCC_AC_3 = "ac-3";
constexpr char* FOURCC_EC_3 = "ec-3"; // Enhanced AC-3
constexpr char* FOURCC_OPUS = "opus";
constexpr char* FOURCC_VORB = "vorb"; // Vorbis
constexpr char* FOURCC_VORB1 = "vor1"; // Vorbis 1
constexpr char* FOURCC_VORB1P = "vo1+"; // Vorbis 1+
constexpr char* FOURCC_VORB2 = "vor2"; // Vorbis 2
constexpr char* FOURCC_VORB2P = "vo2+"; // Vorbis 2+
constexpr char* FOURCC_VORB3 = "vor3"; // Vorbis 3
constexpr char* FOURCC_VORB3P = "vo3+"; // Vorbis 3+
constexpr char* FOURCC_DTS_ = "dts"; // Generic prefix for all dts* fourcc, e.g. dtsx

constexpr std::array AUDIO_FOURCC_LIST = {FOURCC_MP4A, FOURCC_AAC_, FOURCC_AACL,
                                          FOURCC_AC_3, FOURCC_EC_3, FOURCC_OPUS, FOURCC_VORB, FOURCC_VORB1,
                                          FOURCC_VORB1P, FOURCC_VORB2, FOURCC_VORB2P, FOURCC_VORB3,
                                          FOURCC_VORB3P, FOURCC_DTS_};

// Fourcc subtitles definitions

constexpr char* FOURCC_WVTT = "wvtt"; // WebVTT
constexpr char* FOURCC_TTML = "ttml";
// TTML variant for XML type
// In the complete codec description can be presented with or without name and profile
// for example "stpp.ttml.im1t", or only "stpp"
constexpr char* FOURCC_STPP = "stpp";

constexpr std::array SUBTITLES_FOURCC_LIST = {FOURCC_WVTT, FOURCC_TTML, FOURCC_STPP};

/*!
 * \brief Make a FourCC code as unsigned integer value
 * \param fourcc The FourCC code (4 chars)
 * \return The FourCC as unsigned integer value
 */
constexpr uint32_t MakeFourCC(const char* fourcc)
{
  return ((static_cast<uint32_t>(fourcc[0])) | (static_cast<uint32_t>(fourcc[1]) << 8) |
          (static_cast<uint32_t>(fourcc[2]) << 16) | (static_cast<uint32_t>(fourcc[3]) << 24));
}

/*!
 * \brief Convert a fourCC unsigned integer to equivalent string
 * \param fourcc The FourCC
 * \return The FourCC as string value
 */
std::string FourCCToString(const uint32_t fourcc);

/*!
 * \brief Check if a codec string exists in the list, convenient function to check within of strings
 *        e.g find "ttml" return true also when there is a "stpp.ttml.im1t" codec string
 * \param list The list of codecs
 * \param codec The codec string to find
 * \return True if found, otherwise false
 */
bool Contains(const std::set<std::string>& list, std::string_view codec);

/*!
 * \brief Checks whether the codec string exists within a list entry,
 *        e.g check for "ttml" will return true also when there is a "stpp.ttml.im1t" codec string
 * \param list The list of codecs
 * \param codec The codec string to find
 * \param codecStr[OUT] Return the original codec string if found, otherwise empty string
 * \return True if found, otherwise false
 */
bool Contains(const std::set<std::string>& list, std::string_view codec, std::string& codecStr);

/*!
 * \brief Get the description of the video codec contained in the list
 * \param list The list of codecs
 * \return The video codec description, otherwise empty if unsupported
 */
std::string GetVideoDesc(const std::set<std::string>& list);

/*!
 * \brief Determines if the codec string is of type audio, regardless of whether it is a name or fourcc.
 * \param codec The codec string
 * \return True if it is audio type, otherwise false
 */
bool IsAudio(std::string_view codec);

/*!
 * \brief Determines if the codec string is of type video, regardless of whether it is a name or fourcc.
 * \param codec The codec string
 * \return True if it is video type, otherwise false
 */
bool IsVideo(std::string_view codec);

/*!
 * \brief Determines if the codec string contains a fourcc of type subtitles.
 * \param codec The codec string
 * \return True if contains a fourcc of type subtitles, otherwise false
 */
bool IsSubtitleFourCC(std::string_view codec);

}

} // namespace UTILS
