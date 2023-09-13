cmake_minimum_required(VERSION 3.5)
project(webm_parser)

set(webm_parser_sources
    "include/webm/buffer_reader.h"
    "include/webm/callback.h"
    "include/webm/dom_types.h"
    "include/webm/element.h"
    "include/webm/file_reader.h"
    "include/webm/id.h"
    "include/webm/istream_reader.h"
    "include/webm/reader.h"
    "include/webm/status.h"
    "include/webm/webm_parser.h"
    "src/ancestory.cc"
    "src/ancestory.h"
    "src/audio_parser.h"
    "src/bit_utils.cc"
    "src/bit_utils.h"
    "src/block_additions_parser.h"
    "src/block_group_parser.h"
    "src/block_header_parser.cc"
    "src/block_header_parser.h"
    "src/block_more_parser.h"
    "src/block_parser.cc"
    "src/block_parser.h"
    "src/bool_parser.h"
    "src/buffer_reader.cc"
    "src/byte_parser.h"
    "src/callback.cc"
    "src/chapter_atom_parser.h"
    "src/chapter_display_parser.h"
    "src/chapters_parser.h"
    "src/cluster_parser.h"
    "src/colour_parser.h"
    "src/content_enc_aes_settings_parser.h"
    "src/content_encoding_parser.h"
    "src/content_encodings_parser.h"
    "src/content_encryption_parser.h"
    "src/cue_point_parser.h"
    "src/cue_track_positions_parser.h"
    "src/cues_parser.h"
    "src/date_parser.cc"
    "src/date_parser.h"
    "src/ebml_parser.h"
    "src/edition_entry_parser.h"
    "src/element_parser.h"
    "src/file_reader.cc"
    "src/float_parser.cc"
    "src/float_parser.h"
    "src/id_element_parser.cc"
    "src/id_element_parser.h"
    "src/id_parser.cc"
    "src/id_parser.h"
    "src/info_parser.h"
    "src/int_parser.h"
    "src/istream_reader.cc"
    "src/master_parser.cc"
    "src/master_parser.h"
    "src/master_value_parser.h"
    "src/mastering_metadata_parser.h"
    "src/parser.h"
    "src/parser_utils.cc"
    "src/parser_utils.h"
    "src/projection_parser.h"
    "src/recursive_parser.h"
    "src/seek_head_parser.h"
    "src/seek_parser.h"
    "src/segment_parser.cc"
    "src/segment_parser.h"
    "src/simple_tag_parser.h"
    "src/size_parser.cc"
    "src/size_parser.h"
    "src/skip_callback.h"
    "src/skip_parser.cc"
    "src/skip_parser.h"
    "src/slices_parser.h"
    "src/tag_parser.h"
    "src/tags_parser.h"
    "src/targets_parser.h"
    "src/time_slice_parser.h"
    "src/track_entry_parser.h"
    "src/tracks_parser.h"
    "src/unknown_parser.cc"
    "src/unknown_parser.h"
    "src/var_int_parser.cc"
    "src/var_int_parser.h"
    "src/video_parser.h"
    "src/virtual_block_parser.cc"
    "src/virtual_block_parser.h"
    "src/void_parser.cc"
    "src/void_parser.h"
    "src/webm_parser.cc")

add_library(webm_parser STATIC ${webm_parser_sources})

target_include_directories(webm_parser
                           PRIVATE ${PROJECT_SOURCE_DIR}/include
                           PUBLIC ${PROJECT_SOURCE_DIR}/include)

set_target_properties(webm_parser PROPERTIES POSITION_INDEPENDENT_CODE True)

# Fix C++17 Win UWP build error on src/master_parser.h line 156 and 160 with C4996.
# NOTE: Check sometimes libwebm becomes update byself in future to fix about!
if(CORE_SYSTEM_NAME MATCHES windows)
  target_compile_definitions(webm_parser PRIVATE -D_SILENCE_CXX17_ADAPTOR_TYPEDEFS_DEPRECATION_WARNING)
endif()

source_group_by_folder(webm_parser)
