[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)
[![Build Status](https://dev.azure.com/teamkodi/binary-addons/_apis/build/status/xbmc.inputstream.adaptive?branchName=Nexus)](https://dev.azure.com/teamkodi/binary-addons/_build/latest?definitionId=79&branchName=Nexus)
[![Build and run tests](https://github.com/xbmc/inputstream.adaptive/actions/workflows/build.yml/badge.svg?branch=Nexus)](https://github.com/xbmc/inputstream.adaptive/actions/workflows/build.yml)
[![Build Status](https://jenkins.kodi.tv/view/Addons/job/xbmc/job/inputstream.adaptive/job/Nexus/badge/icon)](https://jenkins.kodi.tv/blue/organizations/jenkins/xbmc%2Finputstream.rtmp/branches/)

# InputStream Adaptive add-on for Kodi

This is a [Kodi](https://kodi.tv) input stream add-on which acts as a demuxer for segmented, multi-bitrate internet streams. The most common streaming protocols such as MPEG-DASH, HLS and Microsoft Smooth Streaming are supported.

The add-on also has support for DRM protected streams, such as Google Widevine, Microsoft PlayReady and others, however some are only available on specific operating systems. To use the Google Widevine DRM on non-Android systems is required the installation of the Widevine CDM module library (not included with this add-on).

To enable InputStream playback via your video add-on, you must first configure the *ListItem* properties appropriately.

For test purposes, you can create STRM files with URLs and playback parameters to instruct Kodi to use this InputStream add-on.

*Please refer to the [Wiki pages](https://github.com/xbmc/inputstream.adaptive/wiki) for detailed instructions on how to integrate, build and test with your add-on.*

## WIP Features

#### Bandwidth and resolution:
When using inputstream.adaptive the first time, the selection of stream quality / stream resolution is done with a guess of 4MBit/s. This default value will be updated at the time you watch your first movie by measuring the download speed of the media streams.
Always you start a new video, the average bandwidth of the previous media watched will be taken to calculate the initial stream representation from the set of existing qualities.
If this leads to problems in your environment, you can override / adjust this value using Min. bandwidth in the inputstream.adaptive settings dialog. Setting Min. bandwidth e.g. to 10.000.000, the media selection will never be done with a bandwidth value below
this value.
Currently the complete media is played with the selection from this initial step, adaptive stream changes during a running video is still under development.
There is a new Max. resolution select field in the inputstream.adaptive settings dialog.
Auto will select the best resolution matching to your videoplayer display rect without any limits.
If your display resolution is 720p, you will not be able to watch 1080p videos if there are video representations available closer to 720p.

#### TODO's:
- Adaptive bitrate switching has been implemented in Kodi as of v20, but will continue to be developed over time.
- Automatic / fixed video stream selection depending on max. visible display rect (some work has to be done at the inputstream interface).
- There will be many MPEG-DASH, HSL or Smooth Stream manifest types currently not supported that must be extended.

### Notes:
- This addon uses threads to download segments. The memory consumption is the sum of single segment from each stream currently playing. Refering to known streams it is < 10MB for 720p videos.

## Acknowledgements
InputStream Adaptive exists thanks to the help of many

* Special thanks to the author / creator [@peak3d](https://github.com/peak3d)
* All the **[contributors](https://github.com/xbmc/inputstream.adaptive/graphs/contributors)**
* [Bento4](https://www.bento4.com/) for the great extensible library for mp4 streams

## License
InputStream Adaptive is **[GPLv2 licensed](LICENSE.md)**. You may use, distribute and copy it under the license terms.
