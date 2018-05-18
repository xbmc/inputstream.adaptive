# inputstream.adaptive (2.2.18)

This is an adaptive file addon for kodi's new InputStream Interface.

- this addon is part of the official kodi repository and part of each kodi installation
- configure the addon by adding URL prefixes wich are allowed to be played by this addon
- create a .strm file / or addon with passes an url with sets inputstream.adaptive.manifest_type to either "mpd", "ism" or "hls" and open the strm file in kodi
- or write an addon wich passes an mpd or ism manifest file to kodi

##### Examples:
1.) mpd dash example with one video and one audio stream
- Force inputstream.mpd using a property in strm file: #KODIPROP:inputstreamaddon=inputstream.adaptive
- Select the tye of the manifest using a property in strm file: #KODIPROP:inputstream.adaptive.manifest_type=mpd
- URL to paste into strm file: http://download.tsi.telecom-paristech.fr/gpac/DASH_CONFORMANCE/TelecomParisTech/mp4-live/mp4-live-mpd-AV-BS.mpd

2.) mpd dash example with one video and multiple audio streams
- Force inputstream.mpd using a Property in strm file: #KODIPROP:inputstreamaddon=inputstream.mpd
- Select the tye of the manifest using a property in strm file: #KODIPROP:inputstream.adaptive.manifest_type=mpd
- URL to paste into strm file: http://rdmedia.bbc.co.uk/dash/ondemand/testcard/1/client_manifest-events-multilang.mpd

##### Decrypting:
Decrypting is not implemented. But it is prepared!  
Decrypting takes place in separate decrypter shared libraries, wich are identified by the inputstream.mpd.licensetype listitem property.  
Only one shared decrypter library can be active during playing decrypted media. Building decrypter libraries do not require kodi sources.  
Simply check out the sources of this addon and you are able to build decrypters including full access to existing decrypters implemented in bento4.

##### Bandwidth and resolution:
When using inputstream.adaptive the first time, the selection of stream quality / stream resolution is done with a guess of 4MBit/s. This default value will be updated at the time you watch your first movie by measuring the download speed of the media streams.  
Always you start a new video, the average bandwidth of the previous media watched will be taken to calculate the initial stream representation from the set of existing qualities.  
If this leads to problems in your environment, you can override / adjust this value using Min. bandwidth in the inputstream.adaptive settings dialog. Setting Min. bandwidth e.g. to 10.000.000, the media selection will never be done with a bandwidth value below 
this value.  
Currently the complete media is played with the selection from this initial step, adaptive stream changes during a running video is still under development.  
There is a new Max. resolution select field in the inputstream.adaptive settings dialog.
Auto will select the best resolution matching to your videoplayer display rect without any limits.
If your display resolution is 720p, you will not be able to watch 1080p videos if there are video representations available closer to 720p.  


##### TODO's:
- Adaptive bitrate switching is prepared but currently not yet activated  
- Automatic / fixed video stream selection depending on max. visible display rect (some work has to be done at the inputstream interface).
- DASH implementation of periods (currently only the first period is considered)
- There will be many dash mpd, smoothstream or hls manifest types currently not supported - must be extended. 

##### Notes:
- This addon uses threads to download segments. The memory consumption is the sum of single segment from each stream currently playing. Refering to known streams it is < 10MB for 720p videos.

##### Credits:
[@fernetmenta](github.com/fernetmenta) Best support I ever got regarding streams / codecs and kodi internals.  
[@notspiff](https://github.com/notspiff) Thanks for your ideas / tipps regarding kodi file system  
[bento4 library](https://www.bento4.com/) For me the best library choice for mp4 streams. Well written and extensible!

##### Continuous integration:
[Travis CI build state:](https://travis-ci.org/peak3d) ![alt tag](https://travis-ci.org/peak3d/inputstream.adaptive.svg?branch=master)  
[Appveyor:](https://ci.appveyor.com/project/peak3d) ![alt tag](https://ci.appveyor.com/api/projects/status/ah9s8usgxhangq7o?svg=true)
