# Script to build ISA for GitHub Workflow
# Copyright 2020 @glennguy, @matthuisman, @CastagnaIT

# Required variables to be set in the workflow job (example):
# ADDON_ID="the.addon.id"
# ADDON_REPO="the repository address"
# PLATFORM="windows"
# ARCH="x86_64"
# KODI_VERSION="matrix"

# List of pre-installed software on GitHub workspace virtual enviroment:
# https://github.com/actions/virtual-environments/tree/main/images/linux
# https://github.com/actions/virtual-environments/tree/main/images/win

CONFIGURE_EXTRA_OPTIONS=""
CMAKE_GENERATOR=""
CMAKE_TOOLSET=""
declare -A ARCHS=( ["linux-armv7"]="arm-linux-gnueabihf" \
                   ["linux-aarch64"]="aarch64-linux-gnu" \
                   ["linux-x86_64"]="x86_64-linux" \
                   ["android-armv7"]="arm-linux-androideabi" \
                   ["android-aarch64"]="aarch64-linux-android" \
                   ["darwin-x86_64"]="x86_64-apple-darwin" )

# For NDK version list: https://developer.android.com/ndk/downloads
#                   or: sdkmanager --list
if [[ $KODI_VERSION == "leia" ]]; then
    KODI_BRANCH="Leia"
    ISA_BRANCH="Leia"
    NDK_VER="18.1.5063045"
elif [[ $KODI_VERSION == "matrix" ]]; then
    KODI_BRANCH="master"
    ISA_BRANCH="Matrix"
    NDK_VER="20.1.5948944"
else
    echo "Kodi version $KODI_VERSION not valid, supported versions [leia,matrix]"
    exit 1
fi

case $ARCH in
    x86_64|x86|aarch64|armv7)
        ;;
    *)
        echo "Arch $ARCH not valid, must be one of [x86_64,aarch64,armv7,x86]"
        exit 2
        ;;
esac

case $PLATFORM in
    android|darwin|linux|windows)
        ;;
    *)
        echo "Platform $PLATFORM not valid, must be one of [android,darwin,linux,windows]"
        exit 3
        ;;
esac

### INSTALL PREREQUISITES ###
if [[ $ARCH = aarch64 ]]  && [[ $PLATFORM = linux ]]; then
    sudo apt install -y --no-install-recommends crossbuild-essential-arm64
elif [[ $ARCH = armv7 ]] && [[ $PLATFORM = linux ]]; then
    sudo apt install -y --no-install-recommends crossbuild-essential-armhf
fi

cd ..
ISA_PATH=$(pwd)
echo "ISA_PATH path: $ISA_PATH"
mkdir -p kodi-git
KODI_GIT=$(pwd)/kodi-git
echo "KODI_GIT path: $KODI_GIT"
echo "GITHUB_WORKSPACE path: $GITHUB_WORKSPACE"
echo "GITHUB_WORKSPACE dirname path: $(dirname "$GITHUB_WORKSPACE")"


### CONFIGURE ANDROID TOOLS ###
if [[ $PLATFORM = android ]]; then
    # The SDK is pre-installed in the virtual enviroment
    echo "yes" | sudo ${ANDROID_HOME}/tools/bin/sdkmanager --install "ndk;$NDK_VER" >/dev/null

    CONFIGURE_EXTRA_OPTIONS="--with-ndk-api=21 --with-sdk-path=${ANDROID_SDK_ROOT} --with-ndk-path=${ANDROID_HOME}/ndk/$NDK_VER"
fi


### CLONE KODI XBMC REPOSITORY ###
cd $KODI_GIT
KODI_GIT_XBMC=$(pwd)/xbmc
git clone https://github.com/xbmc/xbmc --branch $KODI_BRANCH --depth 1 $KODI_GIT_XBMC


### CONFIGURE KODI BUILD TOOLS ###
if [[ $PLATFORM = darwin ]]; then
    CONFIGURE_EXTRA_OPTIONS="--with-sdk=10.15 --with-platform=macos"
    if [[ $KODI_VERSION = leia ]]; then
        gsed -i '/10\.14);;/a\          10\.15);;' $KODI_GIT/tools/depends/configure.ac
        CONFIGURE_EXTRA_OPTIONS="--with-sdk=10.15"
    else
        CONFIGURE_EXTRA_OPTIONS="--with-sdk=10.15 --with-platform=macos"
    fi
fi

if [[ $PLATFORM != windows ]]; then
    cd $KODI_GIT_XBMC/tools/depends
    ./bootstrap
    ./configure --host=${ARCHS[$PLATFORM-$ARCH]} --disable-debug --prefix=$KODI_GIT_XBMC/xbmc-depends $CONFIGURE_EXTRA_OPTIONS
    CMAKE_GENERATOR="Unix Makefiles"
else
    case $ARCH in
        x86_64)
            CMAKE_ARCH="x64"
            CMAKE_GENERATOR="Visual Studio 16 2019"
            ;;
        x86)
            CMAKE_ARCH="Win32"
            CMAKE_GENERATOR="Visual Studio 16 2019"
            ;;
        *)
            echo "Arch $ARCH not valid for windows, must be one of [x86_64,x86]"
            exit 4
            ;;
    esac
    CMAKE_TOOLSET="-A $CMAKE_ARCH"
fi


### CONFIGURE BUILD ###
mkdir -p $KODI_GIT_XBMC/tools/depends/target/binary-addons/addons2/$ADDON_ID && cd "$_"
echo "all" > platforms.txt
echo "$ADDON_ID $ADDON_REPO $KODI_BRANCH" > $ADDON_ID.txt

TOOLCHAIN_OPTION=""
if [[ $PLATFORM != windows ]]; then
    TOOLCHAIN_OPTION="-DCMAKE_TOOLCHAIN_FILE=$KODI_GIT_XBMC/cmake/addons/$ADDON_ID/build/depends/share/Toolchain_binaddons.cmake"
    mkdir -p $KODI_GIT_XBMC/cmake/addons/$ADDON_ID/build/depends/share
    cp -f $KODI_GIT_XBMC/tools/depends/target/config-binaddons.site $KODI_GIT_XBMC/cmake/addons/$ADDON_ID/build/depends/share/config.site
    sed "s|@CMAKE_FIND_ROOT_PATH@|$KODI_GIT_XBMC/cmake/addons/$ADDON_ID/build/depends|g" $KODI_GIT_XBMC/tools/depends/target/Toolchain_binaddons.cmake > $KODI_GIT_XBMC/cmake/addons/$ADDON_ID/build/depends/share/Toolchain_binaddons.cmake
    cd $KODI_GIT_XBMC/cmake/addons/$ADDON_ID
else
    mkdir -p $ISA_PATH/build && cd "$_"
fi
cmake -G "$CMAKE_GENERATOR" $CMAKE_TOOLSET -DCMAKE_BUILD_TYPE=Release -DOVERRIDE_PATHS=ON $TOOLCHAIN_OPTION -DADDONS_TO_BUILD=$ADDON_ID -DADDON_SRC_PREFIX=$(dirname "$GITHUB_WORKSPACE") -DADDONS_DEFINITION_DIR=$KODI_GIT_XBMC/tools/depends/target/binary-addons/addons2 -DPACKAGE_ZIP=1 $KODI_GIT_XBMC/cmake/addons


### BULD THE ADD-ON PACKAGE & MOVE/RENAME THE ZIP FILE ###
ZIP_NAME=$ADDON_ID-$PLATFORM-$ARCH-$KODI_VERSION-$(git -C $GITHUB_WORKSPACE describe --always)

if [[ $PLATFORM != windows ]]; then
    make package-$ADDON_ID
    mv $KODI_GIT_XBMC/cmake/addons/$ADDON_ID/$ADDON_ID-prefix/src/$ADDON_ID-build/addon-$ADDON_ID*.zip $KODI_GIT/$ZIP_NAME.zip
else
    cmake --build . --config Release --target package-$ADDON_ID
    mv $HOME/AppData/Local/Temp/addon-$ADDON_ID*.zip $KODI_GIT/$ZIP_NAME.zip
    EXIT_STATUS=$?
    if [[ $EXIT_STATUS -ne 0 ]]; then
        exit 5
    fi
fi
cd $KODI_GIT && ls $ZIP_NAME.zip


### PREPARE THE FILE UNZIPPED (will be zipped dinamically by "upload-artifact" GitHub action) ###
unzip -q $KODI_GIT/$ZIP_NAME.zip -d zip_content
echo "::set-output name=zip-name::$ZIP_NAME"
echo "::set-output name=zip-path::$KODI_GIT/zip_content"
