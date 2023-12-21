[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)
[![Build Status](https://dev.azure.com/teamkodi/binary-addons/_apis/build/status/xbmc.inputstream.adaptive?branchName=Nexus)](https://dev.azure.com/teamkodi/binary-addons/_build/latest?definitionId=79&branchName=Nexus)
[![Build and run tests](https://github.com/xbmc/inputstream.adaptive/actions/workflows/build.yml/badge.svg?branch=Nexus)](https://github.com/xbmc/inputstream.adaptive/actions/workflows/build.yml)
[![Build Status](https://jenkins.kodi.tv/view/Addons/job/xbmc/job/inputstream.adaptive/job/Nexus/badge/icon)](https://jenkins.kodi.tv/blue/organizations/jenkins/xbmc%2Finputstream.adaptive/branches/)

# InputStream Adaptive add-on for Kodi

This is a [Kodi](https://kodi.tv) input stream add-on which acts as a demuxer for segmented, multi-bitrate internet streams. The most common streaming protocols such as MPEG-DASH, HLS and Microsoft Smooth Streaming are supported.

The add-on also has support for DRM protected streams, such as Google Widevine, Microsoft PlayReady and others, however some are only available on specific operating systems. To use the Google Widevine DRM on non-Android systems is required the installation of the Widevine CDM module library (not included with this add-on).

To enable InputStream playback via your video add-on, you must first configure the *ListItem* properties appropriately.

For test purposes, you can create STRM files with URLs and playback parameters to instruct Kodi to use this InputStream add-on.

*Please refer to the [Wiki pages](https://github.com/xbmc/inputstream.adaptive/wiki) for detailed instructions on how to integrate, build and test with your add-on.*

## Add-on WIP status
There are many features that may currently be partially functional or not implemented, we suggest you to open the following Wiki page to better understand the current status of add-on development:
[Add‐on WIP status](https://github.com/xbmc/inputstream.adaptive/wiki/Add%E2%80%90on-WIP-status)

### Notes:
- This addon uses threads to download segments. The memory consumption is the sum of single segment from each stream currently playing. Refering to known streams it is < 10MB for 720p videos.

## Acknowledgements
InputStream Adaptive exists thanks to the help of many

* Special thanks to the author / creator [@peak3d](https://github.com/peak3d)
* All the **[contributors](https://github.com/xbmc/inputstream.adaptive/graphs/contributors)**
* [Bento4](https://www.bento4.com/) for the great extensible library for mp4 streams

## License
InputStream Adaptive is **[GPLv2 licensed](LICENSE.md)**. You may use, distribute and copy it under the license terms.
