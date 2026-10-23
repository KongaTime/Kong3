#!/usr/bin/env bash
# RPCS3 for the PS5 - cross-build my fork, PS5_RPCS3, with the console's compilers.
#
#   rpcs3/build-rpcs3.sh              configure (once) and build RPCS3's emulator core
#   rpcs3/build-rpcs3.sh --configure  configure again from scratch
#   rpcs3/build-rpcs3.sh <target>...  build those targets instead (ninja names)
#
# The source is the fork's checkout beside this repository (../PS5_RPCS3, or
# RPCS3_SRC), with its submodules (OpenCV's is not needed). The build goes to
# build/rpcs3/, and the log of each step beside it.
#
# What the options say, for now (milestone 1: compile RPCS3 unchanged and list
# what the console lacks):
#   - the SDK's own CMake toolchain, Zen 2 code (-march=znver2), frame pointers;
#   - C++20 module scanning (OpenAL Soft uses modules) through
#     rpcs3/clang-scan-deps-ps5, which gives the scanner the flags the SDK's
#     compiler wrapper adds; without a scanner every C++ configure check fails;
#   - LLVM for the PPU and SPU recompilers, built for the console by
#     rpcs3/build-llvm.sh into .deps/native/llvm-ps5/ (static, X86 only);
#   - no Qt frontend, no LTO, no SDL, FAudio, libevdev, ALSA, PulseAudio,
#     GameMode or Discord: the console has none of them, or they come later;
#   - FFmpeg built for the console (rpcs3/build-ffmpeg.sh), in place of
#     upstream's prebuilt archives, and GNU libiconv (rpcs3/build-libiconv.sh):
#     the console's libc has no iconv;
#   - Vulkan from the foundation's headers (external/vulkan) and a placeholder
#     library: the title links RADV itself, with PS5_Vulkan's recipe, and
#     RPCS3 reaches it through the foundation's volk (ps5/third_party/volk);
#
# RPCS3 needs clang 19 or later; the SDK takes the newest llvm-config it finds
# (or LLVM_CONFIG).
#
# RPCS3 is GPL-2.0-only: what this builds is for my own console, never shipped.
#
# Copyright (C) 2026 KongaTime
# SPDX-License-Identifier: MIT

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk=$root/.deps/native/ps5-payload-sdk
build=$root/build/rpcs3

src=${RPCS3_SRC:-}
if [[ -z $src ]]; then
    for candidate in "$root/../PS5_RPCS3" "$root/../ps5_rpcs3"; do
        [[ -f $candidate/rpcs3/CMakeLists.txt ]] && { src=$(cd -- "$candidate" && pwd); break; }
    done
fi
[[ -n $src && -f $src/rpcs3/CMakeLists.txt ]] || {
    echo "error: no PS5_RPCS3 checkout beside this repository (set RPCS3_SRC)" >&2; exit 1; }
[[ -x $sdk/bin/prospero-clang++ ]] || {
    echo "error: no payload SDK in $sdk: run ps5/tools/setup-sdk.sh" >&2; exit 1; }

clang_major=$("$sdk/bin/prospero-llvm-config" --version | cut -d. -f1)
(( clang_major >= 19 )) || {
    echo "error: RPCS3 needs clang 19 or later; the SDK found $clang_major (set LLVM_CONFIG)" >&2; exit 1; }
[[ -x "$("$sdk/bin/prospero-llvm-config" --bindir)/clang-scan-deps" ]] || {
    echo "error: no clang-scan-deps beside the SDK's clang" >&2; exit 1; }
scan_deps=$root/rpcs3/clang-scan-deps-ps5

configure=false
targets=()
for arg in "$@"; do
    case $arg in
        --configure) configure=true ;;
        -*) echo "usage: ${0##*/} [--configure] [target...]" >&2; exit 2 ;;
        *) targets+=("$arg") ;;
    esac
done
(( ${#targets[@]} )) || targets=(rpcs3_emu)

"$root/rpcs3/build-ffmpeg.sh"
"$root/rpcs3/build-libiconv.sh"
"$root/rpcs3/build-llvm.sh"

mkdir -p "$build"
if $configure || [[ ! -f $build/build.ninja ]]; then
    rm -f "$build/CMakeCache.txt"
    # A placeholder for libvulkan: CMake's FindVulkan wants a library to find.
    echo '' | "$sdk/bin/prospero-clang" -x c -c - -o "$build/vulkan-placeholder.o"
    rm -f "$build/libvulkan-placeholder.a"
    "$sdk/bin/prospero-ar" rcs "$build/libvulkan-placeholder.a" "$build/vulkan-placeholder.o"

    flags="-march=znver2 -fno-omit-frame-pointer"
    echo "==> [rpcs3] configuring $src at $(git -C "$src" rev-parse --short HEAD) (clang $clang_major)"
    cmake -S "$src" -B "$build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=OFF \
        -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="$scan_deps" \
        -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" \
        -DUSE_NATIVE_INSTRUCTIONS=OFF -DUSE_LTO=OFF \
        -DWITH_LLVM=ON -DBUILD_LLVM=OFF -DSTATIC_LINK_LLVM=ON \
        -DLLVM_DIR="$root/.deps/native/llvm-ps5/lib/cmake/llvm" \
        -DUSE_FAUDIO=OFF -DUSE_LIBEVDEV=OFF -DUSE_SDL=OFF -DUSE_ALSA=OFF -DUSE_PULSE=OFF \
        -DUSE_DISCORD_RPC=OFF -DUSE_GAMEMODE=OFF -DUSE_PRECOMPILED_HEADERS=OFF \
        -DBUILD_RPCS3_TESTS=OFF \
        -DUSE_SYSTEM_ZLIB=OFF -DUSE_SYSTEM_CURL=OFF -DUSE_SYSTEM_OPENAL=OFF \
        -DUSE_SYSTEM_OPENCV=OFF -DUSE_SYSTEM_SDL=OFF \
        -DPS5_FFMPEG_DIR="$root/.deps/native/ffmpeg-ps5" \
        -DPS5_VOLK_DIR="$root/ps5/third_party/volk" \
        -DPS5_ICONV_DIR="$root/.deps/native/libiconv-ps5" \
        -DPS5_LIBRE_DIR="$root/ps5/libre" \
        -DUSE_VULKAN=ON -DVulkan_INCLUDE_DIR="$root/external" \
        -DVulkan_LIBRARY="$build/libvulkan-placeholder.a" \
        >"$build/configure.log" 2>&1 || {
        grep -n "CMake Error" -A6 "$build/configure.log" >&2
        echo "error: configure failed: $build/configure.log" >&2; exit 1; }
fi

echo "==> [rpcs3] building ${targets[*]}"
ninja -C "$build" -k 0 "${targets[@]}" >"$build/build.log" 2>&1 || {
    echo "error: the build failed: $build/build.log ($(grep -c 'error:' "$build/build.log") errors)" >&2; exit 1; }
echo "==> [rpcs3] built ${targets[*]}"
