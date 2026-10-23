#!/usr/bin/env bash
# RPCS3 for the PS5 - LLVM for the console, for RPCS3's PPU and SPU recompilers.
#
#   rpcs3/build-llvm.sh             build once into .deps/native/llvm-ps5/
#   rpcs3/build-llvm.sh --rebuild   build again from scratch
#
# The source is the LLVM RPCS3 pins (PS5_RPCS3's 3rdparty/llvm/llvm submodule,
# llvm-project at the commit its .gitmodules names; `git submodule update --init
# --depth 1 3rdparty/llvm/llvm` there). Two builds:
#   - build/llvm-host/: llvm-tblgen and its kin for this PC, which the
#     cross-build runs to generate its tables;
#   - build/llvm-ps5/: LLVM's libraries for the console, X86 only, static, with
#     the SDK's CMake toolchain and Zen 2 code, installed to
#     .deps/native/llvm-ps5/ for rpcs3/build-rpcs3.sh to find (LLVM_DIR).
# No tools, tests, examples, zlib, zstd, libxml2 or terminfo: RPCS3 uses LLVM as
# a JIT library and the console has none of those. What the console needs
# changed in LLVM is in rpcs3/llvm-patches/, applied to the checkout here.
#
# LLVM is Apache-2.0 WITH LLVM-exception.
#
# Copyright (C) 2026 KongaTime
# SPDX-License-Identifier: MIT

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk=$root/.deps/native/ps5-payload-sdk
host_build=$root/build/llvm-host
build=$root/build/llvm-ps5
prefix=$root/.deps/native/llvm-ps5

src=${RPCS3_SRC:-}
if [[ -z $src ]]; then
    for candidate in "$root/../PS5_RPCS3" "$root/../ps5_rpcs3"; do
        [[ -f $candidate/rpcs3/CMakeLists.txt ]] && { src=$(cd -- "$candidate" && pwd); break; }
    done
fi
llvm=$src/3rdparty/llvm/llvm/llvm
[[ -f $llvm/CMakeLists.txt ]] || {
    echo "error: no LLVM source in $llvm: git submodule update --init --depth 1 3rdparty/llvm/llvm in PS5_RPCS3" >&2; exit 1; }

# The console's changes to LLVM, as patches (rpcs3/llvm-patches/), applied to the
# checkout once
for patch in "$root"/rpcs3/llvm-patches/*.patch; do
    [[ -f $patch ]] || continue
    if git -C "$llvm/.." apply --check "$patch" 2>/dev/null; then
        git -C "$llvm/.." apply "$patch"
        echo "==> [llvm] applied ${patch##*/}"
    elif ! git -C "$llvm/.." apply --reverse --check "$patch" 2>/dev/null; then
        echo "error: ${patch##*/} does not apply to $llvm/.." >&2; exit 1
    fi
done

rebuild=false
[[ ${1:-} == --rebuild ]] && rebuild=true
# What the build is: the commit, this script and the patches. A change to the
# last two configures and builds again, incrementally
revision=$(git -C "$llvm/.." rev-parse HEAD)
stamp="$revision $(cat "$0" "$root"/rpcs3/llvm-patches/*.patch 2>/dev/null | sha256sum | cut -c1-16)"
if ! $rebuild && [[ -f $prefix/.revision && $(cat "$prefix/.revision") == "$stamp" ]]; then
    echo "==> [llvm] ${revision:0:11} already built in $prefix"
    exit 0
fi
$rebuild && rm -rf "$host_build" "$build" "$prefix"

common=(-G Ninja -DCMAKE_BUILD_TYPE=Release
    -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_PROJECTS=
    -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF
    -DLLVM_INCLUDE_DOCS=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF
    -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_LIBEDIT=OFF
    -DLLVM_ENABLE_LIBPFM=OFF -DLLVM_ENABLE_ASSERTIONS=OFF -DLLVM_ENABLE_WARNINGS=OFF)

# The host's table generators
if [[ ! -x $host_build/bin/llvm-tblgen ]]; then
    echo "==> [llvm] host table generators (${revision:0:11})"
    mkdir -p "$host_build"
    # Any host clang builds them: clang-20 where it is installed, else clang
    host_cc=clang-20 host_cxx=clang++-20
    command -v "$host_cc" > /dev/null || host_cc=clang host_cxx=clang++
    cmake -S "$llvm" -B "$host_build" "${common[@]}" \
        -DCMAKE_C_COMPILER="$host_cc" -DCMAKE_CXX_COMPILER="$host_cxx" \
        >"$host_build/configure.log" 2>&1 || {
        echo "error: host configure failed: $host_build/configure.log" >&2; exit 1; }
    ninja -C "$host_build" llvm-tblgen llvm-min-tblgen >"$host_build/build.log" 2>&1 || {
        echo "error: host build failed: $host_build/build.log" >&2; exit 1; }
fi

# The console's libraries. LLVM's configure checks link with -lm, which the
# SDK does not have (its libc holds the maths): an empty libm.a stands in.
# No dlopen: the SDK declares it, but a title loads no libraries, and LLVM's
# DynamicLibrary then crashed in every PPU compile worker (dlerror's null);
# llvm-patches/0002 gives it the process without one
flags="-march=znver2 -fno-omit-frame-pointer"
echo "==> [llvm] configuring for the console"
mkdir -p "$build/stub-libs"
"$sdk/bin/prospero-ar" rcs "$build/stub-libs/libm.a"
cmake -S "$llvm" -B "$build" "${common[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake" \
    -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_EXE_LINKER_FLAGS="-L$build/stub-libs" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DLLVM_HOST_TRIPLE=x86_64-unknown-freebsd -DLLVM_DEFAULT_TARGET_TRIPLE=x86_64-unknown-freebsd \
    -DLLVM_TABLEGEN="$host_build/bin/llvm-tblgen" \
    -DLLVM_NATIVE_TOOL_DIR="$host_build/bin" \
    -DLLVM_BUILD_TOOLS=OFF -DLLVM_INCLUDE_TOOLS=OFF -DLLVM_INCLUDE_UTILS=OFF \
    -DLLVM_BUILD_UTILS=OFF -DLLVM_ENABLE_THREADS=ON -DLLVM_ENABLE_BACKTRACES=OFF \
    -DLLVM_ENABLE_CRASH_OVERRIDES=OFF -DLLVM_ENABLE_PLUGINS=OFF \
    -DLLVM_BUILD_LLVM_DYLIB=OFF -DLLVM_ENABLE_PIC=ON \
    -DHAVE_DLOPEN=OFF -DHAVE_LIBDL=OFF \
    >"$build/configure.log" 2>&1 || {
    grep -n "CMake Error" -A6 "$build/configure.log" >&2
    echo "error: configure failed: $build/configure.log" >&2; exit 1; }

echo "==> [llvm] building (a long build)"
ninja -C "$build" -k 0 >"$build/build.log" 2>&1 || {
    echo "error: the build failed: $build/build.log ($(grep -c 'error:' "$build/build.log") errors)" >&2; exit 1; }
ninja -C "$build" install >"$build/install.log" 2>&1 || {
    echo "error: install failed: $build/install.log" >&2; exit 1; }
echo "$stamp" >"$prefix/.revision"
echo "==> [llvm] ${revision:0:11} built in $prefix"
