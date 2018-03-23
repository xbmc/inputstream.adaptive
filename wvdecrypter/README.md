# wvdecrypter

This piece of software implements a CencSingleSampleDecrypter wich can be used with inputstream.mpd addon.

- wvdecrypter is developed to decrypt widevine encrypted media content. On most operating systems it is necessary to provide third part software ([lib]w\*devi\*ecdm.dll/so).
- the inputstream.mpd is part of the official kodi repository and has not to be build. wvdecrypter NOT!  
wvdecrypter comes together with the inputstream.mpd source code because of the interface files wich are necessary for compiling wvdecrypter. Beside this the Bento4 library wich comes with inputstream.mpd already has some other cenc decrypter implementations (e.g. clearkey) and can be implemented easily.

##### How to build:
Linux:  
1.) go into the wvdecrypter folder  
2.) cmake .  
3.) make  
This will produce a libssd_wv.so file

Windows:  
1.) open an cygwin or msys terminal window  
2.) go into the wvdecrypter folder  
3.) cmake . -G"Visual Studio 12 2013"  
4.) Open VisualStudio, open the generated .sln solution and compile  
This will produce a ssd_wv.dll file

##### Installation:
- search your kodi addon folder for "inputstream.mpd.[dll / so]
- go into this folder with the file named above
- create a new folder "decrypter"
- copy the shared library from the build step into this new folder "decrypter"
- search your system for [lib]w\*devi\*ecdm.dll/so (asterix's must be replaced) and copy it also into the new decrypters folder.

##### Todo:
- automate things with [lib]w\*devi\*ecdm.dll/so
- remove the need copying [lib]w\*devi\*ecdm.dll/so

With kodi17 + inputstream.mpd + wvdecrypter you are prepared to play files wich are orginated to HTML5 players.

Have fun!