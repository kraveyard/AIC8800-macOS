#!/bin/bash
# Build AIC8800.kext on macOS with Xcode's SDK. Usage: ./build.sh  -> build/AIC8800.kext
set -euo pipefail
cd "$(dirname "$0")"
SDK=$(xcrun --show-sdk-path)
KH="$SDK/System/Library/Frameworks/Kernel.framework/Headers"
OUT=build/AIC8800.kext
rm -rf build && mkdir -p "$OUT/Contents/MacOS"
FLAGS=(-arch x86_64 -mkernel -nostdlib -fno-builtin -fno-common -O2 -Wall
       -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
       -isysroot "$SDK" -I"$KH" -mmacosx-version-min=11.0)
xcrun clang "${FLAGS[@]}" -c kmod_info.c -o build/kmod_info.o
xcrun clang++ "${FLAGS[@]}" -std=gnu++17 -fapple-kext -fno-rtti -fno-exceptions -c AIC8800.cpp -o build/AIC8800.o
xcrun clang++ "${FLAGS[@]}" -fapple-kext -Xlinker -kext build/kmod_info.o build/AIC8800.o -lkmod -lkmodc++ -lcc_kext -o "$OUT/Contents/MacOS/AIC8800"
cp Info.plist "$OUT/Contents/Info.plist"
codesign -f -s - "$OUT"
echo "built $OUT"
