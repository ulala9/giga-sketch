#!/bin/bash
# Giga Sketch: download, build and install the G'MIC-Qt filter window
# (Filters > Start G'MIC-Qt). It is a separate project; Krita ships it
# alongside its releases and so do we. Same source/version as Krita's own
# recipe in 3rdparty_plugins/ext_gmic/CMakeLists.txt.
# Run again after a Giga Sketch build that changes libkritaqmicinterface,
# or after a Fedora Qt update if G'MIC stops loading.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VER=3.7.2.2
SHA=96f318a43e1cb081880dc67cef6651c13ccf952fc56c79c8343ce43d48a3d3f5
DL="$ROOT/deps/src/gmic-dl"
SRC="$DL/gmic-v$VER/gmic-qt"
BUILD="$ROOT/deps/build-gmic"
INSTALL="$ROOT/install"

mkdir -p "$DL"
if [ ! -d "$SRC" ]; then
    curl -fSL -o "$DL/gmic-$VER.tar.gz" \
        "https://github.com/vanyossi/gmic/releases/download/v$VER/gmic-$VER.tar.gz"
    echo "$SHA  $DL/gmic-$VER.tar.gz" | sha256sum -c
    tar -C "$DL" -xzf "$DL/gmic-$VER.tar.gz"
fi

# gmic-qt's translation scripts call plain "lrelease"; Fedora only has
# lrelease-qt6 in /usr/bin, the plain name lives in Qt's own bin folder.
export PATH=/usr/lib64/qt6/bin:$PATH

mkdir -p "$BUILD"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
    -DBUILD_WITH_QT6=ON -DGMIC_QT_HOST=krita-plugin -DENABLE_SYSTEM_GMIC=FALSE \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL" -DCMAKE_INSTALL_LIBDIR=lib64 \
    "-DCMAKE_PREFIX_PATH=$INSTALL;$ROOT/deps/prefix"
ninja -C "$BUILD" install
echo "Installed: $INSTALL/lib64/kritaplugins/krita_gmic_qt.so"
