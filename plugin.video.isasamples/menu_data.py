# -*- coding: utf-8 -*-
"""
    Copyright (C) 2024 Team Kodi
    SPDX-License-Identifier: GPL-2.0-or-later
    See LICENSES/README.md for more information.
"""
# IMPORTANT: Don't use "/" char on menus and streams titles, because it's reserved for paths.
# Dict key to declare a directory menu item
MI_CONFIG = 'menu_config'
# Dict key to declare a playable stream item
SI_CONFIG = 'stream_config'
# SI_CONFIG how to use it:
# It must contains a dictionary to store all ISA properties as is, simply remove the prefix 'inputstream.adaptive.'
# from ISA property name to be added, the only exception is for the manifest url,
# it can be set by using 'manifest_url' key name.
# For DRM protected cases, SI_CONFIG can accept multiple DRM configuration methods at same time,
# this would not be allowed on ISA, but here we allow to set them all in order to test all use cases without modify
# every time the menu data, and so they will be filtered automatically on settings basis.

# Optionals dict keys to add info in the stream item
SI_CODECS = 'codecs'  # string split by ',' of codec names
SI_INFO = 'info'  # text note
SI_FEATURE = 'feature_flags'  # string split by ',' by using flags from STREAM_FEAT dict
SI_ENCRYPT = 'encrypt_flags'  # string split by ',' by using flags from STREAM_ENC dict

# SI_FEATURE flags for common stream features
STREAM_FEAT = {
    'ADP': 'Adaptive a/v streams',
    'ADPV': 'Adaptive video streams',
    'ADPA': 'Adaptive audio streams',
    'SUB': 'Text subtitles (segmented files)',
    'SUBEXT': 'Text subtitles (single external file)',
    'SUBMP4': 'MP4/sidecar subtitles (segmented files)',
    'AUD': 'Audio only',
    'AUDI': 'Audio included to video stream',
    'CMP4': 'Container: MP4',
    'CWEBM': 'Container: WEBM',
}
# SI_ENCRYPT flags for common stream encryption types
STREAM_ENC = {
    'DRMWV': 'Widevine',
    'DRMPR': 'PlayReady',
    'DRMWP': 'WisePlay',
    'DRMCK': 'ClearKey',
    'DRMFP': 'FairPlay',
    'CENC': 'CENC',
    'CBCS': 'CBCS',
    'AES': 'AES-128',
    'SAES': 'SAMPLE-AES',
    'KROT': 'Key rotation',
}

menu_data = {
    'Manifest Dash': {
        MI_CONFIG: {},
        'Dash VOD': {
            MI_CONFIG: {},
            'Flowplayer night [segment base]': {
                SI_FEATURE: 'ADPV',
                SI_CODECS: 'avc1,mp4a',
                SI_CONFIG: {
                    'manifest_url': 'http://edge.flowplayer.org/night1.mpd',
                }
            },
            'Akamaized bunny [multi-period]': {
                SI_CONFIG: {
                    'manifest_url': 'https://dash.akamaized.net/dash264/TestCases/5a/nomor/1.mpd',
                }
            },
            'Axiom sintel [multi-codec]': {
                SI_CONFIG: {
                    'manifest_url': 'http://media.axprod.net/Temp/KVS/media/MPD/Clear/MultiCodec/sintel.mpd'
                }
            },
            'Axiom v7 1080p [subtitles]': {
                SI_FEATURE: 'ADP,CMP4,SUBMP4',
                SI_CODECS: 'avc1,hev1,mp4a,wvtt,stpp',
                SI_CONFIG: {
                    'manifest_url': 'https://media.axprod.net/TestVectors/v7-Clear/Manifest_1080p.mpd',
                }
            },
            'TravelXP [video]': {
                SI_FEATURE: 'ADP,CMP4,CWEBM',
                SI_CODECS: 'vp9,hvc1,mp4a',
                SI_INFO: 'Two adaptive videos VP9 and HEVC',
                SI_CONFIG: {
                    'manifest_url': 'https://travelxp.s.llnwi.net/watch1/61025c11781ce3c543f5abcd/manifest_v4.mpd'
                }
            },
            'Dashif testpic_2s [subtitles]': {
                SI_FEATURE: 'SUBMP4',
                SI_CODECS: 'avc1,mp4a,stpp',
                SI_CONFIG: {
                    'manifest_url': 'https://livesim.dashif.org/dash/vod/testpic_2s/multi_subs.mpd',
                }
            },
            'Akamaized IT1 [subtitles]': {
                SI_FEATURE: 'SUBEXT',
                SI_CODECS: 'avc1,mp4a,ttml',
                SI_CONFIG: {
                    'manifest_url': 'https://dash.akamaized.net/dash264/CTA/imsc1/IT1-20171027_dash.mpd',
                }
            },
            'Akamaized ED_OnDemand_5SecSeg [subtitles]': {
                SI_FEATURE: 'ADP,CMP4,SUBEXT',
                SI_CODECS: 'avc1,mp4a,ttml',
                SI_INFO: 'Two subtitle tracks. Subs seem malformed, async',
                SI_CONFIG: {
                    'manifest_url': 'https://dash.akamaized.net/dash264/TestCases/4b/qualcomm/1/ED_OnDemand_5SecSeg_Subtitles.mpd',
                }
            },
            'Akamaized bbb_30fps 4k': {
                SI_FEATURE: 'ADP,CMP4',
                SI_CONFIG: {
                    'manifest_url': 'https://dash.akamaized.net/akamai/bbb_30fps/bbb_30fps.mpd',
                }
            },
            'Dolby atmos': {
                SI_CONFIG: {
                    'manifest_url': 'https://ott.dolby.com/OnDelKits/DDP/Dolby_Digital_Plus_Online_Delivery_Kit_v1.4.1/Test_Signals/muxed_streams/DASH/OnDemand_MPD/ChID_voices_1280x720p_25fps_h264_6ch_640kbps_ddp_joc.mpd'
                }
            },
            'Dolby atmos multi-bitrate': {
                SI_FEATURE: 'ADP,CMP4',
                SI_CODECS: 'hvc1,dvh1,ec-3',
                SI_INFO: 'Two video tracks HEVC and DV',
                SI_CONFIG: {
                    'manifest_url': 'https://ott.dolby.com/OnDelKits/DDP/Dolby_Digital_Plus_Online_Delivery_Kit_v1.5/Test_Signals/example_streams/DASH/OnDemand/MPD/Holi_25fps_example_1_clean.mpd'
                }
            },
            'Radiant 4k-av1-avc': {
                SI_FEATURE: 'ADP,CMP4,CWEBM',
                SI_CODECS: 'av01,avc1,opus',
                SI_INFO: 'Two video tracks AV1 (WEBM) and AVC (MP4), audio OPUS (WEBM)',
                SI_CONFIG: {
                    'manifest_url': 'https://www.radiantmediaplayer.com/media/dash/4k-av1-avc/manifest.mpd',
                    'manifest_headers': 'User-Agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:98.0) Gecko/20100101 Firefox/98.0&Host=www.radiantmediaplayer.com&Accept=text/html,application/xhtml+xml,application/xml&Upgrade-Insecure-Requests=1&Accept-Encoding=gzip,defalte,br&Connection=keep-alive',
                    'stream_headers': 'User-Agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:98.0) Gecko/20100101 Firefox/98.0&Host=www.radiantmediaplayer.com&Accept=text/html,application/xhtml+xml,application/xml&Upgrade-Insecure-Requests=1&Accept-Encoding=gzip,defalte,br&Connection=keep-alive',
                }
            },
            'Bitmovin "av01"': {
                SI_FEATURE: 'ADPV,CWEBM',
                SI_CODECS: 'av01,opus',
                SI_INFO: 'All streams use WEBM',
                SI_CONFIG: {
                    'manifest_url': 'https://storage.googleapis.com/bitmovin-demos/av1/stream_chrome.mpd',
                }
            },
            'Uni Beauty_4sec 4k [multi-codec]': {
                SI_FEATURE: 'ADP',
                SI_INFO: 'VP9 (WEBM), HEVC (MP4), H264 (MP4), AV1 (WEBM), No audio',
                SI_CONFIG: {
                    'manifest_url': 'https://ftp.itec.aau.at/datasets/mmsys18/testing/Beauty_4sec/multi-codec.mpd',
                }
            },
            'Bitmovin "av1"': {
                SI_FEATURE: 'ADPV,CWEBM',
                SI_CODECS: 'av1,opus',
                SI_INFO: 'All streams use WEBM',
                SI_CONFIG: {
                    'manifest_url': 'https://storage.googleapis.com/bitmovin-demos/av1/stream.mpd',
                }
            },
            'Akamaized spring 4k [segment base]': {
                SI_CONFIG: {
                    'manifest_url': 'https://dash.akamaized.net/akamai/streamroot/050714/Spring_4Ktest.mpd'
                }
            },
            'Akamaized LastSegmentNumber': {
                SI_INFO: 'To test last segment signal on MP4 box',
                SI_CONFIG: {
                    'manifest_url': 'http://dash.akamaized.net/dash264/TestCasesIOP41/LastSegmentNumber/1/manifest_last_segment_num.mpd'
                }
            },
            'Dashif audio only': {
                SI_CONFIG: {
                    'manifest_url': 'https://livesim.dashif.org/dash/vod/testpic_2s/audio.mpd'
                }
            },
            'YouTube [SegmentList + SegmentTimeline]': {
                SI_FEATURE: 'ADPA',
                SI_INFO: 'VP9 (WEBM), MP4A (MP4)',
                SI_CONFIG: {
                    'manifest_url': 'http://www.youtube.com/api/manifest/dash/id/bf5bb2419360daf1/source/youtube?as=fmp4_audio_clear,webm2_sd_hd_clear&sparams=ip,ipbits,expire,source,id,as&ip=0.0.0.0&ipbits=0&expire=19000000000&signature=249B04F79E984D7F86B4D8DB48AE6FAF41C17AB3.7B9F0EC0505E1566E59B8E488E9419F253DDF413&key=ik0',
                }
            },
            'Edgesuite ElephantsDream [subtitles] ': {
                SI_FEATURE: 'SUBEXT',
                SI_CODECS: 'avc1,mp4a,vtt',
                SI_CONFIG: {
                    'manifest_url': 'http://dash.edgesuite.net/akamai/test/caption_test/ElephantsDream/elephants_dream_480p_heaac5_1.mpd',
                }
            },
            'Exoplayer captions2 [subtitles]': {
                SI_FEATURE: 'SUBMP4',
                SI_CODECS: 'avc1,mp4a,wvtt',
                SI_INFO: 'Three subtitles tracks',
                SI_CONFIG: {
                    'manifest_url': 'http://media.axprod.net/ExoPlayer/Captions2/Manifest.mpd',
                }
            },
            'Dashif testpic_2s CEA-608 caption tracks (eng-swe)': {
                SI_CONFIG: {
                    'manifest_url': 'https://livesim.dashif.org/dash/vod/testpic_2s/cea608.mpd',
                }
            },
        },
        'Dash Live': {
            MI_CONFIG: {},
            'Dashif segtimeline_1 [SegmentTemplate + SegmentTimeline]': {
                SI_CONFIG: {
                    'manifest_url': 'https://livesim2.dashif.org/livesim2/segtimeline_1/testpic_2s/Manifest.mpd'
                }
            },
            'Dashif periods_20 multi-period [SegmentTemplate - no timeline]': {
                SI_INFO: 'A new period every 3 min (20 times/hour)',
                SI_CONFIG: {
                    'manifest_url': 'https://livesim.dashif.org/livesim/periods_20/testpic_2s/Manifest.mpd'
                }
            },
            'Dashif ato_10 [SegmentTemplate - no timeline]': {
                SI_CONFIG: {
                    'manifest_url': 'https://livesim2.dashif.org/livesim2/ato_10/testpic_2s/Manifest.mpd'
                }
            },
        },
        'Dash VOD with DRM': {
            MI_CONFIG: {},
            'Google Tears [CBCS]': {
                SI_ENCRYPT: 'CBCS,DRMWV',
                SI_CONFIG: {
                    'manifest_url': 'https://storage.googleapis.com/wvmedia/cbcs/h264/tears/tears_aes_cbcs.mpd',
                    'license_type': 'com.widevine.alpha',
                    'license_key': 'https://proxy.uat.widevine.com/proxy?provider=widevine_test||R{SSM}|R',
                    'drm_legacy': 'com.widevine.alpha|https://proxy.uat.widevine.com/proxy?provider=widevine_test',
                    'drm': '{"com.widevine.alpha": {"license": {"server_url": "https://proxy.uat.widevine.com/proxy?provider=widevine_test"}}}'
                }
            },
            'Axiom 1080p [CBCS]': {
                SI_ENCRYPT: 'CBCS,DRMWV,DRMPR',
                SI_INFO: 'DRM config set to widevine',
                SI_CONFIG: {
                    'manifest_url': 'https://media.axprod.net/TestVectors/v9-MultiFormat/Encrypted_Cbcs/Manifest_1080p.mpd',
                    'license_type': 'com.widevine.alpha',
                    'license_key': 'https://drm-widevine-licensing.axtest.net/AcquireLicense|X-AxDRM-Message=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ2ZXJzaW9uIjoxLCJjb21fa2V5X2lkIjoiNjllNTQwODgtZTllMC00NTMwLThjMWEtMWViNmRjZDBkMTRlIiwibWVzc2FnZSI6eyJ0eXBlIjoiZW50aXRsZW1lbnRfbWVzc2FnZSIsInZlcnNpb24iOjIsImxpY2Vuc2UiOnsiYWxsb3dfcGVyc2lzdGVuY2UiOnRydWV9LCJjb250ZW50X2tleXNfc291cmNlIjp7ImlubGluZSI6W3siaWQiOiJmOGM4MGMyNS02OTBmLTQ3MzYtODEzMi00MzBlNWM2OTk0Y2UiLCJlbmNyeXB0ZWRfa2V5IjoiaVhxNDlaODlzOGRDajBqbTJBN1h6UT09IiwidXNhZ2VfcG9saWN5IjoiUG9saWN5IEEifV19LCJjb250ZW50X2tleV91c2FnZV9wb2xpY2llcyI6W3sibmFtZSI6IlBvbGljeSBBIiwicGxheXJlYWR5Ijp7Im1pbl9kZXZpY2Vfc2VjdXJpdHlfbGV2ZWwiOjE1MCwicGxheV9lbmFibGVycyI6WyI3ODY2MjdEOC1DMkE2LTQ0QkUtOEY4OC0wOEFFMjU1QjAxQTciXX19XX19.k9OlwW0rUwuf5d5Eb0iO98AFR3qp7qKdFzSbg2PQj78|R{SSM}|',
                    'drm_legacy': 'com.widevine.alpha|https://drm-widevine-licensing.axtest.net/AcquireLicense|X-AxDRM-Message=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ2ZXJzaW9uIjoxLCJjb21fa2V5X2lkIjoiNjllNTQwODgtZTllMC00NTMwLThjMWEtMWViNmRjZDBkMTRlIiwibWVzc2FnZSI6eyJ0eXBlIjoiZW50aXRsZW1lbnRfbWVzc2FnZSIsInZlcnNpb24iOjIsImxpY2Vuc2UiOnsiYWxsb3dfcGVyc2lzdGVuY2UiOnRydWV9LCJjb250ZW50X2tleXNfc291cmNlIjp7ImlubGluZSI6W3siaWQiOiJmOGM4MGMyNS02OTBmLTQ3MzYtODEzMi00MzBlNWM2OTk0Y2UiLCJlbmNyeXB0ZWRfa2V5IjoiaVhxNDlaODlzOGRDajBqbTJBN1h6UT09IiwidXNhZ2VfcG9saWN5IjoiUG9saWN5IEEifV19LCJjb250ZW50X2tleV91c2FnZV9wb2xpY2llcyI6W3sibmFtZSI6IlBvbGljeSBBIiwicGxheXJlYWR5Ijp7Im1pbl9kZXZpY2Vfc2VjdXJpdHlfbGV2ZWwiOjE1MCwicGxheV9lbmFibGVycyI6WyI3ODY2MjdEOC1DMkE2LTQ0QkUtOEY4OC0wOEFFMjU1QjAxQTciXX19XX19.k9OlwW0rUwuf5d5Eb0iO98AFR3qp7qKdFzSbg2PQj78',
                    'drm': '{"com.widevine.alpha": {"license": {"server_url": "https://drm-widevine-licensing.axtest.net/AcquireLicense|X-AxDRM-Message=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ2ZXJzaW9uIjoxLCJjb21fa2V5X2lkIjoiNjllNTQwODgtZTllMC00NTMwLThjMWEtMWViNmRjZDBkMTRlIiwibWVzc2FnZSI6eyJ0eXBlIjoiZW50aXRsZW1lbnRfbWVzc2FnZSIsInZlcnNpb24iOjIsImxpY2Vuc2UiOnsiYWxsb3dfcGVyc2lzdGVuY2UiOnRydWV9LCJjb250ZW50X2tleXNfc291cmNlIjp7ImlubGluZSI6W3siaWQiOiJmOGM4MGMyNS02OTBmLTQ3MzYtODEzMi00MzBlNWM2OTk0Y2UiLCJlbmNyeXB0ZWRfa2V5IjoiaVhxNDlaODlzOGRDajBqbTJBN1h6UT09IiwidXNhZ2VfcG9saWN5IjoiUG9saWN5IEEifV19LCJjb250ZW50X2tleV91c2FnZV9wb2xpY2llcyI6W3sibmFtZSI6IlBvbGljeSBBIiwicGxheXJlYWR5Ijp7Im1pbl9kZXZpY2Vfc2VjdXJpdHlfbGV2ZWwiOjE1MCwicGxheV9lbmFibGVycyI6WyI3ODY2MjdEOC1DMkE2LTQ0QkUtOEY4OC0wOEFFMjU1QjAxQTciXX19XX19.k9OlwW0rUwuf5d5Eb0iO98AFR3qp7qKdFzSbg2PQj78"}}}'
                }
            },
            'Google tears [default_KID on DRM CP]': {
                SI_INFO: 'The default_KID is set on DRM ContentProtection tag',
                SI_ENCRYPT: 'DRMWV',
                SI_CONFIG: {
                    'manifest_url': 'https://storage.googleapis.com/wvmedia/cenc/vp9/tears/tears.mpd',
                    'license_type': 'com.widevine.alpha',
                    'license_key': 'https://proxy.uat.widevine.com/proxy?video_id=2015_tears&provider=widevine_test||R{SSM}|R',
                    'drm_legacy': 'com.widevine.alpha|https://proxy.uat.widevine.com/proxy?video_id=2015_tears&provider=widevine_test',
                    'drm': '{"com.widevine.alpha": {"license": {"server_url": "https://proxy.uat.widevine.com/proxy?video_id=2015_tears&provider=widevine_test"}}}'
                }
            },
            'Google tears [default_KID on common CP]': {
                SI_INFO: 'The default_KID is set on Common MP4 ContentProtection tag',
                SI_ENCRYPT: 'DRMWV',
                SI_CONFIG: {
                    'manifest_url': 'https://storage.googleapis.com/wvmedia/cenc/h264/tears/tears.mpd',
                    'license_type': 'com.widevine.alpha',
                    'license_key': 'https://proxy.uat.widevine.com/proxy?video_id=2015_tears&provider=widevine_test||R{SSM}|R',
                    'drm_legacy': 'com.widevine.alpha|https://proxy.uat.widevine.com/proxy?video_id=2015_tears&provider=widevine_test',
                    'drm': '{"com.widevine.alpha": {"license": {"server_url": "https://proxy.uat.widevine.com/proxy?video_id=2015_tears&provider=widevine_test"}}}'
                }
            },
            'Google angel one': {
                SI_ENCRYPT: 'DRMWV',
                SI_INFO: 'Multiple audio and subtitles tracks',
                SI_CONFIG: {
                    'manifest_url': 'https://storage.googleapis.com/shaka-demo-assets/angel-one-widevine/dash.mpd',
                    'license_type': 'com.widevine.alpha',
                    'license_key': 'https://cwip-shaka-proxy.appspot.com/no_auth',
                    'drm_legacy': 'com.widevine.alpha|https://cwip-shaka-proxy.appspot.com/no_auth',
                    'drm': '{"com.widevine.alpha": {"license": {"server_url": "https://cwip-shaka-proxy.appspot.com/no_auth"}}}'
                }
            },
            'Axiom multiDRM [multi-drm, key rotation]': {
                SI_ENCRYPT: 'DRMWV,DRMPR,KROT',
                SI_INFO: 'Config set to Widevine',
                SI_CONFIG: {
                    'manifest_url': 'https://media.axprod.net/TestVectors/v6.1-MultiDRM/Manifest_1080p.mpd',
                    'license_type': 'com.widevine.alpha',
                    'license_key': 'https://drm-widevine-licensing.axtest.net/AcquireLicense|X-AxDRM-Message=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJ2ZXJzaW9uIjoxLCJjb21fa2V5X2lkIjoiNjllNTQwODgtZTllMC00NTMwLThjMWEtMWViNmRjZDBkMTRlIiwibWVzc2FnZSI6eyJ0eXBlIjoiZW50aXRsZW1lbnRfbWVzc2FnZSIsImtleXMiOlt7ImlkIjoiNmU1YTFkMjYtMjc1Ny00N2Q3LTgwNDYtZWFhNWQxZDM0YjVhIn1dfX0.yF7PflOPv9qHnu3ZWJNZ12jgkqTabmwXbDWk_47tLNE|R{SSM}|',
                    'drm_legacy': 'com.widevine.alpha|https://drm-widevine-licensing.axtest.net/AcquireLicense|X-AxDRM-Message=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJ2ZXJzaW9uIjoxLCJjb21fa2V5X2lkIjoiNjllNTQwODgtZTllMC00NTMwLThjMWEtMWViNmRjZDBkMTRlIiwibWVzc2FnZSI6eyJ0eXBlIjoiZW50aXRsZW1lbnRfbWVzc2FnZSIsImtleXMiOlt7ImlkIjoiNmU1YTFkMjYtMjc1Ny00N2Q3LTgwNDYtZWFhNWQxZDM0YjVhIn1dfX0.yF7PflOPv9qHnu3ZWJNZ12jgkqTabmwXbDWk_47tLNE',
                    'drm': '{"com.widevine.alpha": {"license": {"server_url": "https://drm-widevine-licensing.axtest.net/AcquireLicense", "req_headers": "X-AxDRM-Message=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJ2ZXJzaW9uIjoxLCJjb21fa2V5X2lkIjoiNjllNTQwODgtZTllMC00NTMwLThjMWEtMWViNmRjZDBkMTRlIiwibWVzc2FnZSI6eyJ0eXBlIjoiZW50aXRsZW1lbnRfbWVzc2FnZSIsImtleXMiOlt7ImlkIjoiNmU1YTFkMjYtMjc1Ny00N2Q3LTgwNDYtZWFhNWQxZDM0YjVhIn1dfX0.yF7PflOPv9qHnu3ZWJNZ12jgkqTabmwXbDWk_47tLNE"}}}'
                }
            },
            'Axiom multiDRM [multi-drm] WV': {
                SI_ENCRYPT: 'DRMWV,DRMPR',
                SI_INFO: 'Config set to Widevine',
                SI_CONFIG: {
                    'manifest_url': 'https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey/Manifest_1080p.mpd',
                    'license_type': 'com.widevine.alpha',
                    'license_key': 'https://drm-widevine-licensing.axtest.net/AcquireLicense|X-AxDRM-Message=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ2ZXJzaW9uIjoxLCJjb21fa2V5X2lkIjoiYjMzNjRlYjUtNTFmNi00YWUzLThjOTgtMzNjZWQ1ZTMxYzc4IiwibWVzc2FnZSI6eyJ0eXBlIjoiZW50aXRsZW1lbnRfbWVzc2FnZSIsImZpcnN0X3BsYXlfZXhwaXJhdGlvbiI6NjAsInBsYXlyZWFkeSI6eyJyZWFsX3RpbWVfZXhwaXJhdGlvbiI6dHJ1ZX0sImtleXMiOlt7ImlkIjoiOWViNDA1MGQtZTQ0Yi00ODAyLTkzMmUtMjdkNzUwODNlMjY2IiwiZW5jcnlwdGVkX2tleSI6ImxLM09qSExZVzI0Y3Iya3RSNzRmbnc9PSJ9XX19.FAbIiPxX8BHi9RwfzD7Yn-wugU19ghrkBFKsaCPrZmU|R{SSM}|',
                    'drm_legacy': 'com.widevine.alpha|https://drm-widevine-licensing.axtest.net/AcquireLicense|X-AxDRM-Message=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ2ZXJzaW9uIjoxLCJjb21fa2V5X2lkIjoiYjMzNjRlYjUtNTFmNi00YWUzLThjOTgtMzNjZWQ1ZTMxYzc4IiwibWVzc2FnZSI6eyJ0eXBlIjoiZW50aXRsZW1lbnRfbWVzc2FnZSIsImZpcnN0X3BsYXlfZXhwaXJhdGlvbiI6NjAsInBsYXlyZWFkeSI6eyJyZWFsX3RpbWVfZXhwaXJhdGlvbiI6dHJ1ZX0sImtleXMiOlt7ImlkIjoiOWViNDA1MGQtZTQ0Yi00ODAyLTkzMmUtMjdkNzUwODNlMjY2IiwiZW5jcnlwdGVkX2tleSI6ImxLM09qSExZVzI0Y3Iya3RSNzRmbnc9PSJ9XX19.FAbIiPxX8BHi9RwfzD7Yn-wugU19ghrkBFKsaCPrZmU',
                    'drm': '{"com.widevine.alpha": {"license": {"server_url": "https://drm-widevine-licensing.axtest.net/AcquireLicense", "req_headers": "X-AxDRM-Message=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ2ZXJzaW9uIjoxLCJjb21fa2V5X2lkIjoiYjMzNjRlYjUtNTFmNi00YWUzLThjOTgtMzNjZWQ1ZTMxYzc4IiwibWVzc2FnZSI6eyJ0eXBlIjoiZW50aXRsZW1lbnRfbWVzc2FnZSIsImZpcnN0X3BsYXlfZXhwaXJhdGlvbiI6NjAsInBsYXlyZWFkeSI6eyJyZWFsX3RpbWVfZXhwaXJhdGlvbiI6dHJ1ZX0sImtleXMiOlt7ImlkIjoiOWViNDA1MGQtZTQ0Yi00ODAyLTkzMmUtMjdkNzUwODNlMjY2IiwiZW5jcnlwdGVkX2tleSI6ImxLM09qSExZVzI0Y3Iya3RSNzRmbnc9PSJ9XX19.FAbIiPxX8BHi9RwfzD7Yn-wugU19ghrkBFKsaCPrZmU"}}}'
                }
            },
            'Axiom multiDRM [multi-drm] PR': {
                SI_ENCRYPT: 'DRMWV,DRMPR',
                SI_INFO: 'Config set to PlayReady',
                'stream_config': {
                    'manifest_url': 'https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey/Manifest.mpd',
                    'drm_legacy': 'com.microsoft.playready|https://drm-playready-licensing.axtest.net/AcquireLicense|X-AxDRM-Message=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ2ZXJzaW9uIjoxLCJjb21fa2V5X2lkIjoiYjMzNjRlYjUtNTFmNi00YWUzLThjOTgtMzNjZWQ1ZTMxYzc4IiwibWVzc2FnZSI6eyJ0eXBlIjoiZW50aXRsZW1lbnRfbWVzc2FnZSIsInZlcnNpb24iOjIsImxpY2Vuc2UiOnsiYWxsb3dfcGVyc2lzdGVuY2UiOnRydWV9LCJjb250ZW50X2tleXNfc291cmNlIjp7ImlubGluZSI6W3siaWQiOiI4MDM5OWJmNS04YTIxLTQwMTQtODA1My1lMjdlNzQ4ZTk4YzAiLCJlbmNyeXB0ZWRfa2V5IjoibGlOSnFWYVlrTmgrTUtjeEpGazdJZz09IiwidXNhZ2VfcG9saWN5IjoiUG9saWN5IEEifSx7ImlkIjoiOTA5NTNlMDktNmNiMi00OWEzLWEyNjAtN2E1ZmVmZWFkNDk5IiwiZW5jcnlwdGVkX2tleSI6ImtZdEhIdnJyZkNNZVZkSjZMa2Jrbmc9PSIsInVzYWdlX3BvbGljeSI6IlBvbGljeSBBIn0seyJpZCI6IjBlNGRhOTJiLWQwZTgtNGE2Ni04YzNmLWMyNWE5N2ViNjUzMiIsImVuY3J5cHRlZF9rZXkiOiI3dzdOWkhITE1nSjRtUUtFSzVMVE1RPT0iLCJ1c2FnZV9wb2xpY3kiOiJQb2xpY3kgQSJ9LHsiaWQiOiI1ODVmMjMzZi0zMDcyLTQ2ZjEtOWZhNC02ZGMyMmM2NmEwMTQiLCJlbmNyeXB0ZWRfa2V5IjoiQWM0VVVtWXRCSjVuUFE5TjE1cmMzZz09IiwidXNhZ2VfcG9saWN5IjoiUG9saWN5IEEifSx7ImlkIjoiNDIyMmJkNzgtYmM0NS00MWJmLWI2M2UtNmY4MTRkYzM5MWRmIiwiZW5jcnlwdGVkX2tleSI6Ik82Rk8wZnFTV29wcDdiamMvRDRsTUE9PSIsInVzYWdlX3BvbGljeSI6IlBvbGljeSBBIn1dfSwiY29udGVudF9rZXlfdXNhZ2VfcG9saWNpZXMiOlt7Im5hbWUiOiJQb2xpY3kgQSIsInBsYXlyZWFkeSI6eyJtaW5fZGV2aWNlX3NlY3VyaXR5X2xldmVsIjoxNTAsInBsYXlfZW5hYmxlcnMiOlsiNzg2NjI3RDgtQzJBNi00NEJFLThGODgtMDhBRTI1NUIwMUE3Il19fV19fQ.nMT-_Xor0ROh9sSAbDGu5nsMjrsCJ1W7FZGcr2xpqBY',
                    'drm': '{"com.microsoft.playready": {"license": {"server_url": "https://drm-playready-licensing.axtest.net/AcquireLicense", "req_headers": "X-AxDRM-Message=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ2ZXJzaW9uIjoxLCJjb21fa2V5X2lkIjoiYjMzNjRlYjUtNTFmNi00YWUzLThjOTgtMzNjZWQ1ZTMxYzc4IiwibWVzc2FnZSI6eyJ0eXBlIjoiZW50aXRsZW1lbnRfbWVzc2FnZSIsInZlcnNpb24iOjIsImxpY2Vuc2UiOnsiYWxsb3dfcGVyc2lzdGVuY2UiOnRydWV9LCJjb250ZW50X2tleXNfc291cmNlIjp7ImlubGluZSI6W3siaWQiOiI4MDM5OWJmNS04YTIxLTQwMTQtODA1My1lMjdlNzQ4ZTk4YzAiLCJlbmNyeXB0ZWRfa2V5IjoibGlOSnFWYVlrTmgrTUtjeEpGazdJZz09IiwidXNhZ2VfcG9saWN5IjoiUG9saWN5IEEifSx7ImlkIjoiOTA5NTNlMDktNmNiMi00OWEzLWEyNjAtN2E1ZmVmZWFkNDk5IiwiZW5jcnlwdGVkX2tleSI6ImtZdEhIdnJyZkNNZVZkSjZMa2Jrbmc9PSIsInVzYWdlX3BvbGljeSI6IlBvbGljeSBBIn0seyJpZCI6IjBlNGRhOTJiLWQwZTgtNGE2Ni04YzNmLWMyNWE5N2ViNjUzMiIsImVuY3J5cHRlZF9rZXkiOiI3dzdOWkhITE1nSjRtUUtFSzVMVE1RPT0iLCJ1c2FnZV9wb2xpY3kiOiJQb2xpY3kgQSJ9LHsiaWQiOiI1ODVmMjMzZi0zMDcyLTQ2ZjEtOWZhNC02ZGMyMmM2NmEwMTQiLCJlbmNyeXB0ZWRfa2V5IjoiQWM0VVVtWXRCSjVuUFE5TjE1cmMzZz09IiwidXNhZ2VfcG9saWN5IjoiUG9saWN5IEEifSx7ImlkIjoiNDIyMmJkNzgtYmM0NS00MWJmLWI2M2UtNmY4MTRkYzM5MWRmIiwiZW5jcnlwdGVkX2tleSI6Ik82Rk8wZnFTV29wcDdiamMvRDRsTUE9PSIsInVzYWdlX3BvbGljeSI6IlBvbGljeSBBIn1dfSwiY29udGVudF9rZXlfdXNhZ2VfcG9saWNpZXMiOlt7Im5hbWUiOiJQb2xpY3kgQSIsInBsYXlyZWFkeSI6eyJtaW5fZGV2aWNlX3NlY3VyaXR5X2xldmVsIjoxNTAsInBsYXlfZW5hYmxlcnMiOlsiNzg2NjI3RDgtQzJBNi00NEJFLThGODgtMDhBRTI1NUIwMUE3Il19fV19fQ.nMT-_Xor0ROh9sSAbDGu5nsMjrsCJ1W7FZGcr2xpqBY"}}}'
                }
            },
            'Bitmovin art of motion parkour': {
                SI_ENCRYPT: 'DRMWV',
                SI_CONFIG: {
                    'manifest_url': 'https://bitmovin-a.akamaihd.net/content/art-of-motion_drm/mpds/11331.mpd',
                    'license_type': 'com.widevine.alpha',
                    'license_key': 'https://cwip-shaka-proxy.appspot.com/no_auth|R{SSM}|R',
                    'drm_legacy': 'com.widevine.alpha|https://cwip-shaka-proxy.appspot.com/no_auth',
                    'drm': '{"com.widevine.alpha": {"license": {"server_url": "https://cwip-shaka-proxy.appspot.com/no_auth"}}}'
                }
            },
            'Google sintel [key rotation]': {
                SI_ENCRYPT: 'DRMWV,KROT',
                SI_INFO: 'Key rotation every 3 minutes',
                SI_CONFIG: {
                    'manifest_url': 'https://storage.googleapis.com/playerinfra/wv/sintel_3min_rotate.mpd',
                    'license_type': 'com.widevine.alpha',
                    'license_key': 'https://widevine-proxy.appspot.com/proxy',
                    'drm_legacy': 'com.widevine.alpha|https://widevine-proxy.appspot.com/proxy',
                    'drm': '{"com.widevine.alpha": {"license": {"server_url": "https://widevine-proxy.appspot.com/proxy"}}}'
                }
            },
            'Microsoft appleset [CBCS and CENC]': {
                SI_ENCRYPT: 'CBCS,DRMPR',
                SI_INFO: 'Video use CBCS, Audio use CENC',
                'stream_config': {
                    'manifest_url': 'https://test.playready.microsoft.com/media/dash/APPLEENC_CBCS_BBB_1080p/1080p.mpd',
                    'license_type': 'com.microsoft.playready',
                    'license_key': 'https://test.playready.microsoft.com/service/rightsmanager.asmx?cfg=(persist:false,ck:W31bfVt9W31bfVt9W31bfQ==,ckt:aescbc)',
                    'drm_legacy': 'com.microsoft.playready|https://test.playready.microsoft.com/service/rightsmanager.asmx?cfg=(persist:false,ck:W31bfVt9W31bfVt9W31bfQ==,ckt:aescbc)',
                    'drm': '{"com.microsoft.playready": {"license": {"server_url": "https://test.playready.microsoft.com/service/rightsmanager.asmx?cfg=(persist:false,ck:W31bfVt9W31bfVt9W31bfQ==,ckt:aescbc)"}}}'
                }
            },
            'Axiom v7multiDRM [clear key, license url embedded]': {
                SI_ENCRYPT: 'DRMCK',
                SI_INFO: 'License url embedded on ContentProtection Laurl tag. Expired ssl certificate on license url, disabled SSL verify peer.',
                SI_CONFIG: {
                    'manifest_url': 'https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey/Manifest_1080p_ClearKey.mpd',
                    'drm_legacy': 'org.w3.clearkey',
                    'drm': '{"org.w3.clearkey": {}}',
                    'config': '{"ssl_verify_peer":false}'
                }
            },
            'Google angel one [clear key, keys on property]': {
                SI_ENCRYPT: 'DRMCK',
                SI_CONFIG: {
                    'manifest_url': 'https://storage.googleapis.com/shaka-demo-assets/angel-one-clearkey/dash.mpd',
                    'drm_legacy': 'org.w3.clearkey|feedf00deedeadbeeff0baadf00dd00d:00112233445566778899aabbccddeeff,1234f00deedeadbeeff0baadf00dd00d:8899aabbccddeeff8899aabbccddeeff',
                    'drm': '{"org.w3.clearkey": {"license": {"keyids": {"feedf00deedeadbeeff0baadf00dd00d": "00112233445566778899aabbccddeeff", "1234f00deedeadbeeff0baadf00dd00d": "8899aabbccddeeff8899aabbccddeeff"}}}}',
                }
            },
            'Bitmovin art of motion [widevine to clear key, keys on property]': {
                SI_ENCRYPT: 'DRMCK',
                SI_INFO: 'Override widevine content protection to use clear key',
                SI_CONFIG: {
                    'manifest_url': 'https://cdn.bitmovin.com/content/assets/art-of-motion_drm/mpds/11331.mpd',
                    'drm_legacy': 'org.w3.clearkey|eb676abbcb345e96bbcf616630f1a3da:100b6c20940f779a4589152b57d2dacb',
                    'drm': '{"org.w3.clearkey": {"license": {"keyids": {"eb676abbcb345e96bbcf616630f1a3da": "100b6c20940f779a4589152b57d2dacb"}}}}',
                }
            },
            'TEST WV': {
                SI_ENCRYPT: 'DRMWV,DRMPR',
                SI_INFO: 'Config set to Widevine',
                SI_CONFIG: {
                    'manifest_url': 'https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey/Manifest_1080p.mpd',
                    'drm': '{"com.widevine.alpha":{"license": {"server_url": "https://drm-widevine-licensing.axtest.net/AcquireLicense","req_data": "eyJ0b2tlbiI6ICJwcmNiNzgyY2Y0NmI0MDM1YmU4Y2Y4ZGMyOTFmNmU0ODQ3MTkyZTViYjk1OGMwMDI5MjJmMTNlNmY1NThhZDM1NTk3ODViYWNmYjZjNjQxMjUyNTk2YTBlNTRlZWFhN2MwNWU4MGFjODI4MDJhMTNlZDk1NmVmMjY1MzIwZTA1NTQyMjA3NjA1MDkyNzk2YjY0NWIyYWMyMjAxMzVmM2MwNzQ2NzZlZjA0YmVjOTcyZTJmZmI3MWIyMGU5M2VjNWY4OGZhNDI3ZDA0ZDViYTU3OTU5ZGZiNGY1OWFkZDhiMGU1N2VmZDZlODBjYTk0ZGFmODZhMDM1OWYxNTY5NzE3MDViNTZmNWE4N2FlODQ4YjE1Y2I5NGY1YTRkNmU1ODA2NmYyYTcxNDk3OTg5ZTEwNWE4YjgzNjE2NzlhNTcyMjQwNmU3YjczMTk2N2JiNzNmZjAzMTBiZWJhNzU3NzA0ZTJlYWI3YmRlZmM2YmU1OGJhMzNjYWEyNzQ3ZDY1NWExNjIzYWRmMTUzMDNiOWI2NWJjZThmYzg0MGY1MzdiMzc5YmI4MzhlNWM5YjRmYzg1NWE3YWFlMzFmZjFlZjJkZTlhODE5N2RhM2YyNTQ1ODBhMDZkYmVlNTk1MDI3MWEzOTVlOGFiNWI5MGM4ZWYxNGY5ZGM2NGViODE0NzM3ZTczYmVkNzc2MTg5YmJmOTJlMzY5NjcyNTVlNzc1YmNhNWU1YmZmY2M1MTkzYjFmOTNmZDNiMGFmODc0NzE0MjE1MDA2MjY1OWNjZjViOWRkYzQ0NzM2MjQwNjlkZTA5ZjkyMjE4YjZiYTY1MGEyZjc4ZDA5MzNmODc1ODhmZjI0YWQwZTVmYWU4MjNlYjNjYzUyMjMwZTIyNWQ1YzliMDU5MzQ0MzkyNzJhNzYyMjE5NzAzY2Q2ZWQ0ZTI5OTRhN2Y4ZmZiMjJhNjI5MGJkYzdlYzQ1ZDU4MTAyNzk0NmU4M2EwMDg2ODI4MjY0NTdlY2YyMTRiNmQ1M2I3MTFmMTYyNzQ1ZDBmYjdjNzMyMTJiMzQxYTAzYjU4ZjQ5NGM0ODkzMTE1N2NmNDFkMzZhYWNiMTNhYmRiOTAwZjgzZGI4ZGJjMzNlMjQ5OWE3ZDhkZGE4YWFmZDg5ZGQ5NjI4Njg4YzFlOGQxYWE0ZTRhMjEyNDdkYmQ5MTE4MDE1ODExNzAwY2I0OTI5NDY2MjBkM2U0NzE0N2NjOTE5ZjNlYmVmY2E3NDk5YmFjNWYzMDM3NDU0NDk3NmI2ZTBkYWVhNThjNzllMmExMWE1YzNmMjdlOWM0ZjY2OWVjOWUzYTcxOWI3NjA4MDZiYzQ0NGE4NjYwZWU2N2U1MDhlMzY4OWEyMGJiNDNmYjdlZGUxNjY2NzFjZjQ0MWVlZGNkMDBhYWViYTFkNjIyYzJkNjJkOGY3YWI3YWZmZGFkNDNiMDkwM2I5NGNkY2NiNTUwMGEzN2E3MGIwNDJhMTg1Y2Q2Y2EwNTRjMWM4NTY0ZTM5YjMxMGVkYmI0ODE1YTRmMjc0NzljYzk3YzI4NjEwYTQyYjkwZjdkMGM5NGZkNDI0ZjkyOTBiZWU1YWIzMmI0ZmVmNTI4NTc5N2Q4ZWYwZDBjNTZlZTIwYjM0YWMyOGQxZjQyMjFlM2NlZjRmZDlmNjk4ZmFkNGU3MmVhMDNmMmJjM2RkNGYxMGEyMTBlZjQ1NDFiOTQ2ZTE4M2YxZDI0ZTZlY2JiOGFjMGZmYWQ0M2QxZTIxZGNlM2ExZWI2NmQ0NjFkMGExMzAyYWM1M2I3ZGM5NDcwMzhlNGY5ZWM1ZWNmNTQzMTMyODY2Nzg1NDI3NzNiYTZjYzUwNmQwODZhZDA1NDZiYWJlYmJjYmRjZWFiZGI2MWE4Mjk3YzU1NWQwZmNkZGYwZTY4ZjE4ZGU0YWZkMTRlMTA0ZjFhODYyMTBkOTc0OGQ4ZTE4YTI5ODBhZmQ1NTdjNzdkMTU1MjUyNThhNDI1MzAyYTgyNzJmMWZiOTFlNDJlNWM1NzUyZDZhMGRjMTU3NDI5ZmEzZGViZTYyNTM1ZmRmMDRkYjI5OGM1MzVlM2QyZmI1NDAwNzc3ZDcwZGIyNWY0OWJkNDg4ZDg0NmNhODY2NzNhNDc0YThhNzIwOThjNWQyNWM2YjM4N2IzNDI0NWMxZDViYjJiN2ZmYTc2MTNlZjFlZDM5Mjg2Zjg5MGFiZDUzNjM5NzcxN2Q0ZGEwZTVhOGVhZWQ2NzkyYjI3NjA0ZDlmYTFhZWY1ZjgwYjA2MGFkNTljZWJlOTIxYzZkYzQxM2Q3YTE5MDQ3MjhkNzlhMDE3OTUzYmQ1MmMwZmYxOWE0M2YxNWNkZjRlMThlMTNhYjE5ZmRlNGQ0NDEwN2U3ZjQ5ZjIwMjI5ZDE4ZTRhNTRlM2Q0ZGJjNzRiZWQyNWM4OWE2NWZiODQzMGZjZWVkZDIyNGQ3ZmFhNzlhNzMxNmY3YWFjNTdjNmVkNjA2NjM3NzY1NWVjOTVlMjA3NzAxYzViYzkzMDVhNzU0MGIzOGQwNjVlZDc2ZDliZjdkZTAyZDFhMGRkYWJjODczNDE5MmNmYTkyMTM5YjJlNDZjMjljODJmMzdkMTM5NTc2ZDAzMTQ2NDMxMmRhN2RlMzQ3NTM3N2NlNTE1ZmEyZGNjYjBlNDY2ZGYwMGI4MDYwMjJiYjYwMDZhZjk2MWUzOWEwODY1YjIzMTI1ZmQ1ZTIwNjUyYjY3OTI4MGI5YWY1M2U4N2RmMWJlZjIxNDcxY2IwMDk0YzJlYmI2OTc4M2M2M2Y1YTlkNTVkYTk5ZjA3ZTlkYWMxMTc2YjMwYjg4OGY0MzM3N2FmNjJmZDVlMjgyNzE3MzlmZDBjN2JlZjQ4NmM3YWFjNjg1MWNmZDBlOTAyYTg5OGNjMGM0OWQ0OWIxNDQ1ZjZhYjNkY2QxYmVlODY3OGM5NTU5OWQ2OTVlZTlmNzkzMmRmOWQ4OTIwMzc1NTUzNDk4MjFiMGNlNWZiZjc5Y2ZhNWNkNGRlNmY3ZTAxZTJlMjZhNGQ2Y2M3MGMzMThjOGUyMjk2NjA5YmE1N2VjNWVlNTA1YTMwNzc1Mzk2YWY1ZDE1ZDA5YjliYTM0MTE5NzBlMjZmMTAwMmM5MzYzMmQ2NGM1ZGYxMzUyZjA0MGQ4Mjc5YTY4ZGUxMDQ5Y2UxNzI3ZGZiYjcxMzI2Y2M1NGM1OWRlNjAxZTYyNzYyZmI1NTA0ZTQzNjMzMjQxN2JkZmZlNmZiZDQ1NTc3ZWRkNWIzMWZhZjcyMzA3MjFlOTg4NTEwYTA2ZDhjOWRjMTc2MTlmNzVkYTQ4MjU3MzU2NGY1NGRiYzM0MGVlYjk2YWVjNjgzOTMwYWZhODI2ZjA3Njc2NzkyMGM4OTQ1MDY4NWU3NjkyODI1NTk3OWUyMDE3ZmJiMGE3YWRjMTNhMjM4YWU2MzQ2NTgyN2Q5NzEzYThhNzY3MDRkNTA3ZmQyZDM5NmRlZDllM2RmODhhOTE0MTg4NWNmNzAxMzZhMmE5MDIyNWM4NGM3MGRiZDc4YzM1ZTliNjhjZGIyMTJkODhlNWUwMjBiNDliYmQ5ZjQ0NDlkMzc1NDY5YTk5YTJjNzJmMDU3ZGE1MTVmNDRkOGNlZmQyMzdjYzZlYTUxMWExMWE0NGE1Y2EyZWQ4ZjEzMmZiOTc5M2E1YjIwNjBmYzcxOGZjOTVhMmM1Y2IwNmJkNzZhNGM5YTg5NWFmZWRjOTQ1MTY0ZTFmYzRmNmQzNTdmZjVlNTU1MzJjMjBkZjYzOGQyNzI2YmVjZGRmOTJjNTc0NzVlYzRjNGIzMDI2OGZhMDk4MjVkMWNlOGIyYjhhMWZmZjkxYjVmYzFkMWFjOTAxMDE3OGU4MGE4YTBhMzJiMDVjZjRkM2RhNTM1YTU4ZDJlZTY1ZDNjZDhjMDgzZTlkZjc3ODdhNDEwMjMyMGMzMWFlYmJiOTNjZTY2ODBlZWM5NGFkODNiZTYwZmE3OWM0ZDRhYWZlYjkyYjJjZWY4YmU3NzUxMTUwOWJkNzEzOGZhY2YxMDlmOTJhZGUxNDM3MDg4NzQzMDRlZmQ4NzcxMTA1NzE1NzcyN2MxZmIyMjMyYjM5Y2Y4NzNkYzU5ODMyNzNmZWMwY2U0ZTg4MDkzZWI5MjgwM2Y2NDg2MzJhNDNjNTFhZTUwZjRkZTM0YmQ0OTY0YTY5NGEzN2IzYzRkNjUzOTA2MzgxYmM5NmNjZGQ1NjZiNzhhOGFhODZiNDA4ZTlkZDgxZmMzNGJkNzVlZWRmMjMzMTlhOGRmNjNmMTU4ODE1NmIwMDJiYjE2MTZhODNkNzU2NjJjZjE0MDc1MjI0OGQyYWVjMjhiN2E2NzBiZDYyM2E1MWI5MTEzZjBmN2I0YThiOTExOGQ2NzFkNmZlNjg2OTJhMmEzOTEzNWM1M2NkNzFjMTk2MDA5NjM0M2JmMjVlYWNkYjIwODJlZjU2YmNkMjAzZTkzOGNkYjg3MTZkZDBhZGM5ODBlZWI1MmMxNTAxODA2NGMxZmY2ZDMyZDMwZDliMjI4OTFiOWNjYTViNjRjNzFhNmI3ZmNlYjkwMTkzM2E3ODUwYyIsICJjaGFsbGVuZ2VfYmFzZTY0IjogIntDSEEtQjY0fSJ9"}}}'
                }
            },
        },
    },
    'Manifest HLS': {
        MI_CONFIG: {},
        'HLS VOD': {
            MI_CONFIG: {},
            'Google shaka demo [multi-audio-codecs, subtitles]': {
                SI_FEATURE: 'ADPV,SUB',
                SI_CODECS: 'avc1,mp4a,ac-3,ec-3,wvtt',
                SI_CONFIG: {
                    'manifest_url': 'https://storage.googleapis.com/shaka-demo-assets/apple-advanced-stream-ts/master.m3u8'
                }
            },
            'Theoplayer elephants dream [subtitles]': {
                SI_FEATURE: 'ADPV,SUB',
                SI_CODECS: 'avc1,mp4a,wvtt',
                SI_INFO: 'Two subtitles tracks',
                SI_CONFIG: {
                    'manifest_url': 'http://cdn.theoplayer.com/video/elephants-dream/playlist-single-audio.m3u8'
                }
            },
            'Longtailvideo oceans [audio included and also separate track]': {
                SI_FEATURE: 'ADPV,AUDI',
                SI_ENCRYPT: 'AES',
                SI_CONFIG: {
                    'manifest_url': 'https://playertest.longtailvideo.com/adaptive/oceans_aes/oceans_aes.m3u8'
                }
            },
            'Theoplayer big bunny - key for each segment [media playlist]': {
                SI_ENCRYPT: 'AES',
                SI_INFO: 'This is media m3u8 manifest, not master m3u8',
                SI_CONFIG: {
                    'manifest_url': 'https://cdn.theoplayer.com/video/big_buck_bunny_encrypted/stream-800/index.m3u8'
                }
            },
            'UnifiedStreaming tears of steel': {
                SI_FEATURE: 'ADPV,SUB',
                SI_ENCRYPT: 'AES',
                SI_CODECS: 'avc1,hvc1,aac-lc,wvtt',
                SI_INFO: 'Two subtitles tracks',
                SI_CONFIG: {
                    'manifest_url': 'https://demo.unified-streaming.com/k8s/features/stable/video/tears-of-steel/tears-of-steel-aes.ism/.m3u8'
                }
            },
            'Apple bipbop_adv [multi-codec]': {
                SI_FEATURE: 'ADPV,SUB',
                SI_CODECS: 'avc1,hvc1,ac-3,ec-3,wvtt',
                SI_CONFIG: {
                    'manifest_url': 'https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_adv_example_hevc/master.m3u8'
                }
            },
            'Apple adv_dv_atmos [multi-codec]': {
                SI_FEATURE: 'ADPV,SUB',
                SI_CODECS: 'avc1,hvc1,dvh1,ac-3,ec-3,aac-lc,he-aac,wvtt',
                SI_INFO: 'Audio have also atmos, multiple subtitles tracks',
                SI_CONFIG: {
                    'manifest_url': 'https://devstreaming-cdn.apple.com/videos/streaming/examples/adv_dv_atmos/main.m3u8'
                }
            },
            'UnifiedStreaming tears of steel - impaired subs': {
                SI_FEATURE: 'ADPV,SUB',
                SI_CONFIG: {
                    'manifest_url': 'https://demo.unified-streaming.com/k8s/features/stable/video/tears-of-steel/tears-of-steel-hoh-subs.ism/.m3u8',
                }
            },
            'UnifiedStreaming tears of steel [sample-aes, key rotation]': {
                SI_ENCRYPT: 'SAES',
                SI_CONFIG: {
                    'manifest_url': 'https://demo.unified-streaming.com/k8s/keyrotation/stable/keyrotation/keyrotation.isml/.m3u8'
                }
            },
            'UnifiedStreaming tears of steel [sample-aes]': {
                SI_ENCRYPT: 'SAES',
                SI_CONFIG: {
                    'manifest_url': 'https://demo.unified-streaming.com/k8s/features/stable/video/tears-of-steel/tears-of-steel-sample-aes.ism/.m3u8'
                }
            },
            'Videojs singlefiles [byte range segments]': {
                SI_CONFIG: {
                    'manifest_url': 'https://videojs-test-1.s3.eu-central-1.amazonaws.com/HLS_SingleFiles/master.m3u8'
                }
            },
            'Longtailvideo elephants dream': {
                SI_FEATURE: 'ADPV,SUB',
                SI_CODECS: 'avc1,aac,wvtt',
                SI_INFO: 'Multiple audio and subtitles tracks',
                SI_CONFIG: {
                    'manifest_url': 'https://playertest.longtailvideo.com/adaptive/elephants_dream_v4/index.m3u8'
                }
            },
            'Apple bipbop [media playlist]': {
                SI_CONFIG: {
                    'manifest_url': 'http://devimages.apple.com/iphone/samples/bipbop/gear1/prog_index.m3u8'
                }
            },
            'AWS bipbop advanced [subtitles]': {
                SI_FEATURE: 'ADPV,SUB',
                SI_INFO: 'Multiple subtitles tracks with different flags',
                SI_CONFIG: {
                    'manifest_url': 'https://s3.amazonaws.com/_bc_dml/example-content/bipbop-advanced/bipbop_16x9_variant.m3u8',
                }
            },
            'Kaltura sample 4/3 resolution': {
                SI_CONFIG: {
                    'manifest_url': 'http://cdnbakmi.kaltura.com/p/243342/sp/24334200/playManifest/entryId/0_uka1msg4/flavorIds/1_vqhfu6uy,1_80sohj7p/format/applehttp/protocol/http/a.m3u8',
                }
            },
            'mux.dev ADS deltatre': {
                SI_INFO: 'Multiple periods, the first and the last one are AES-128 encrypted',
                SI_CONFIG: {
                    'manifest_url': 'https://test-streams.mux.dev/dai-discontinuity-deltatre/manifest.m3u8',
                }
            },
            'mux.dev Tears of Steel, IMSC Captions': {
                SI_FEATURE: 'ADPV,SUBMP4',
                SI_CODECS: 'avc1,mp4a,wvtt',
                SI_INFO: 'Subtitle TTML MP4 container',
                SI_CONFIG: {
                    'manifest_url': 'https://test-streams.mux.dev/tos_ismc/main.m3u8',
                }
            },
        },
        'HLS VOD with DRM': {
            MI_CONFIG: {},
            'Ezdrm bunny [multi-drm]': {
                SI_ENCRYPT: 'DRMWV,DRMFP',
                SI_INFO: 'DRM config set to widevine',
                SI_CONFIG: {
                    'manifest_url': 'https://drm-test-cf.softvelum.com/live_ezdrm/bunny/playlist.m3u8',
                    'license_type': 'com.widevine.alpha',
                    'license_key': 'https://widevine-dash.ezdrm.com/widevine-php/widevine-foreignkey.php?pX=B03B45||R{SSM}|R',
                    'drm_legacy': 'com.widevine.alpha|https://widevine-dash.ezdrm.com/widevine-php/widevine-foreignkey.php?pX=B03B45',
                    'drm': '{"com.widevine.alpha": {"license": {"server_url": "https://widevine-dash.ezdrm.com/widevine-php/widevine-foreignkey.php?pX=B03B45"}}}'
                }
            },
            'Google angel one [multi-period]': {
                SI_ENCRYPT: 'DRMWV',
                SI_CONFIG: {
                    'manifest_url': 'https://storage.googleapis.com/shaka-demo-assets/angel-one-widevine-hls/hls.m3u8',
                    'license_type': 'com.widevine.alpha',
                    'license_key': 'https://cwip-shaka-proxy.appspot.com/no_auth||R{SSM}|R',
                    'drm_legacy': 'com.widevine.alpha|https://cwip-shaka-proxy.appspot.com/no_auth',
                    'drm': '{"com.widevine.alpha": {"license": {"server_url": "https://cwip-shaka-proxy.appspot.com/no_auth"}}}'
                }
            },
            'Microsoft applenc [CBCS]': {
                SI_ENCRYPT: 'CBCS,DRMPR',
                SI_CONFIG: {
                    'manifest_url': 'https://test.playready.microsoft.com/media/dash/APPLEENC_CBCS_BBB_1080p/1080p_alternate.m3u8',
                    'license_type': 'com.microsoft.playready',
                    'license_key': 'https://test.playready.microsoft.com/service/rightsmanager.asmx?cfg=(persist:false,ck:W31bfVt9W31bfVt9W31bfQ==,ckt:aescbc)',
                    'drm_legacy': 'com.microsoft.playready|https://test.playready.microsoft.com/service/rightsmanager.asmx?cfg=(persist:false,ck:W31bfVt9W31bfVt9W31bfQ==,ckt:aescbc)',
                    'drm': '{"com.microsoft.playready": {"license": {"server_url": "https://test.playready.microsoft.com/service/rightsmanager.asmx?cfg=(persist:false,ck:W31bfVt9W31bfVt9W31bfQ==,ckt:aescbc)"}}}'
                }
            },
            'Google angel one [clear key, keys on manifest]': {
                SI_ENCRYPT: 'DRMCK',
                SI_CONFIG: {
                    'manifest_url': 'https://storage.googleapis.com/shaka-demo-assets/angel-one-sample-aes-ctr-multiple-key/manifest.m3u8',
                    'drm_legacy': 'org.w3.clearkey',
                    'drm': '{"org.w3.clearkey": {}}',
                }
            },
            'Google angel one [clear key, keys on property]': {
                SI_ENCRYPT: 'DRMCK',
                SI_CONFIG: {
                    'manifest_url': 'https://storage.googleapis.com/shaka-demo-assets/angel-one-sample-aes-ctr-multiple-key/manifest.m3u8',
                    'drm_legacy': 'org.w3.clearkey|abba271e8bcf552bbd2e86a434a9a5d9:abba271e8bcf552bbd2e86a434a9a5d9,a4631a153a443df9eed0593043db7519:a4631a153a443df9eed0593043db7519',
                    'drm': '{"org.w3.clearkey": {"license": {"keyids": {"abba271e8bcf552bbd2e86a434a9a5d9": "abba271e8bcf552bbd2e86a434a9a5d9", "a4631a153a443df9eed0593043db7519": "a4631a153a443df9eed0593043db7519"}}}}',
                }
            },
        },
    },
    'Manifest Smooth Streaming': {
        MI_CONFIG: {},
        'ISM VOD': {
            MI_CONFIG: {},
            'UnifiedStreaming Tears of steel [subtitles]': {
                SI_FEATURE: 'ADP,SUBMP4',
                SI_INFO: 'Multiple subtitles',
                SI_CONFIG: {
                    'manifest_url': 'https://demo.unified-streaming.com/k8s/features/stable/video/tears-of-steel/tears-of-steel-multiple-subtitles.ism/Manifest',
                }
            },
        },
        'ISM VOD with DRM': {
            MI_CONFIG: {},
            'Microsoft SuperSpeedway': {
                SI_ENCRYPT: 'DRMPR',
                SI_INFO: 'WRM-HEADER has outdated license url, its replaced by the provided one',
                SI_CONFIG: {
                    'manifest_url': 'https://test.playready.microsoft.com/smoothstreaming/SSWSS720H264PR/SuperSpeedway_720.ism/Manifest',
                    'license_type': 'com.microsoft.playready',
                    'license_key': 'https://test.playready.microsoft.com/service/rightsmanager.asmx?cfg=(persist:false,sl:150)',
                    'drm_legacy': 'com.microsoft.playready|https://test.playready.microsoft.com/service/rightsmanager.asmx?cfg=(persist:false,sl:150)',
                    'drm': '{"com.microsoft.playready": {"license": {"server_url": "https://test.playready.microsoft.com/service/rightsmanager.asmx?cfg=(persist:false,sl:150)"}}}'
                }
            },
            'Microsoft tears of steel 4k': {
                SI_ENCRYPT: 'DRMPR',
                SI_INFO: 'WRM-HEADER has outdated license url, its replaced by the provided one',
                SI_CONFIG: {
                    'manifest_url': 'https://test.playready.microsoft.com/media/profficialsite/tearsofsteel_4k.ism.smoothstreaming/manifest',
                    'license_type': 'com.microsoft.playready',
                    'license_key': 'https://test.playready.microsoft.com/service/rightsmanager.asmx?cfg=(persist:false,sl:150)',
                    'drm_legacy': 'com.microsoft.playready|https://test.playready.microsoft.com/service/rightsmanager.asmx?cfg=(persist:false,sl:150)',
                    'drm': '{"com.microsoft.playready": {"license": {"server_url": "https://test.playready.microsoft.com/service/rightsmanager.asmx?cfg=(persist:false,sl:150)"}}}'
                }
            },
        },
        'ISM Live': {
            MI_CONFIG: {},
            'UnifiedStreaming k8s_stable': {
                SI_CONFIG: {
                    'manifest_url': 'https://demo.unified-streaming.com/k8s/live/stable/live.isml/Manifest',
                }
            },
        },
    },
}


def find_encrypt_entries(data, encrypt_flag=None):
    """Traverse all dictionaries on json data to find all streams with encrypt flags"""

    def traverse_dict(d, path, curr_key):
        """
        Traverse a dictionary
        :param d: Json data dict
        :param path: The current json path
        :param curr_key: The current dict key (e.g. the title of menu)
        :return: a yield of tuple as
                 (the full json path, the parent dict key name, the current dict key, the current dict value)
        """
        if isinstance(d, dict):
            if SI_CONFIG in d and SI_ENCRYPT in d and (encrypt_flag is None or encrypt_flag in d[SI_ENCRYPT]):
                yield '/'.join(path), path[0], curr_key, d
            else:
                if curr_key:
                    path.append(curr_key)
                for k, v in d.items():
                    yield from traverse_dict(v, path, k)
                if path:
                    del path[-1]
    yield from traverse_dict(data, [], '')


def find_feature_entries(data, feat_flags):
    """Traverse all dictionaries on json data to find all streams with some feature flags"""
    if not feat_flags:
        return
    def traverse_dict(d, path, curr_key):
        """
        Traverse a dictionary
        :param d: Json data dict
        :param path: The current json path
        :param curr_key: The current dict key (e.g. the title of menu)
        :return: a yield of tuple as
                 (the full json path, the parent dict key name, the current dict key, the current dict value)
        """
        if isinstance(d, dict):
            if SI_CONFIG in d and SI_FEATURE in d and any(value in d[SI_FEATURE] for value in feat_flags):
                yield '/'.join(path), path[0], curr_key, d
            else:
                if curr_key:
                    path.append(curr_key)
                for k, v in d.items():
                    yield from traverse_dict(v, path, k)
                if path:
                    del path[-1]
    yield from traverse_dict(data, [], '')
