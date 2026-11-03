#!/usr/bin/env bash
# RoachPet build script (MinGW-w64)
set -e
cd "$(dirname "$0")"

GXX=${GXX:-g++}
WINDRES=${WINDRES:-windres}

mkdir -p build bin

echo "[1/2] compiling resources..."
"$WINDRES" -I res res/app.rc -O coff -o build/app.res

echo "[2/2] compiling & linking..."
"$GXX" -std=c++17 -municode -mwindows -Os -s -static \
    -finput-charset=UTF-8 -fexec-charset=UTF-8 \
    -Wall -Wno-unused-variable -Wno-unused-parameter \
    -Isrc \
    src/config.cpp src/render.cpp src/desktop.cpp src/zorder.cpp src/bug.cpp src/app.cpp \
    build/app.res \
    -o bin/RoachPet.exe \
    -lgdiplus -lole32 -loleaut32 -luuid -lcomctl32 -lshell32 -lshlwapi \
    -lwinmm -lgdi32 -luser32 -ladvapi32 -ldwmapi

echo "done -> bin/RoachPet.exe"
ls -lh bin/RoachPet.exe
