#!/usr/bin/env bash
# PS5 Vulkan Template - link the title and package its folder.
#
#   link-title.sh BUILD_DIR PS5_VULKAN_DIR SDK OBJECT...
#
# Run by the build (ps5/CMakeLists.txt) once the objects are compiled. The link
# is PS5_Vulkan's: its RADV release archive (RADV, ACO, NIR and Mesa's runtime,
# built from PS5_Mesa), its link recipe (tools/radv-link.sh: the platform layer
# of this title's SDK pin, the heap and thread wraps, the libc names bound to ps5_*),
# its CRT, its native tool (ELF -> the console's fake SELF) and its libc.prx,
# as its CTS title links them. The title folder, dist/<TITLE_ID>/, gets the
# signed eboot.bin, sce_sys/, the shaders of the samples linked in and the
# assets (tools/build-assets.py).
#
# Copyright (C) 2026 Mihawk
# SPDX-License-Identifier: MIT

set -euo pipefail
[[ -n ${LINK_TRACE:-} ]] && set -x

work=$1 vulkan=$2 sdk=$3
shift 3
objects=("$@")
ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
archive=${RADV_ARCHIVE:-$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a}
export PS5_CLANG=${PS5_CLANG:-$(command -v clang || true)}
tool="$vulkan/build/host/ps5-native-tool"
native="$vulkan/tooling/native"
param="$ps5/sce_sys/param.json"

for file in "$archive" "$tool" "$param" "$sdk/bin/prospero-lld" "$vulkan/tools/radv-link.sh" \
        "$vulkan/runtime/libc.prx" "$work/samples.txt"; do
    [[ -e $file ]] || { echo "missing: $file" >&2; exit 2; }
done
mkdir -p "$work/link/obj" "$work/link/stubs"
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vulkan/tooling/prospero-clang18" "$@"; }

# The CRT. The C++ runtime is libc++abi's (the samples throw and catch), as in
# PS5_Vulkan's CTS title. The start-up, layout, symbol policy and libc.prx are
# the title's own (ps5/libre/: MIT, docs/distributable-build.md), proven on my
# console with build 156; PS5_LIBRE_START=0 takes PS5_Vulkan's instead
libre_start=${PS5_LIBRE_START:-1}
if [[ $libre_start == 1 ]]; then
    cc -std=c11 -O2 -fno-stack-protector -c "$ps5/libre/crt/start.c" -o "$work/link/obj/app_crt.o"
    symbol_policy="$ps5/libre/ld/title-symbols.map"
else
    cc -std=c++20 -O2 -fno-exceptions -fno-rtti -c "$native/app_crt.cpp" -o "$work/link/obj/app_crt.o"
    symbol_policy="$native/app-symbols.map"
fi
# RADV calls AGC, which the SDK has no stubs for: these name its imports.
stub() {
    local library=$1 source=$2
    cc -std=c11 -O2 -fPIC -c "$vulkan/$source" -o "$work/link/obj/${library}_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "${library}.prx" \
        -o "$work/link/stubs/${library}.so" "$work/link/obj/${library}_stub.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c

# shellcheck source=/dev/null
source "$vulkan/tools/radv-link.sh"
radv_link_recipe "$vulkan" "$sdk" "$archive" || exit 2
[[ $libre_start == 1 ]] && radv_linker_script=(-T "$ps5/libre/ld/ps5-title.ld")
# The libc names the PS5 libre layer implements (ps5/libre/: MIT), bound to its
# libre_<name> in place of the platform layer's ps5_<name> the recipe binds
# (the recipe's flags, and the title's own, which come with its objects)
libre_bound=(__cxa_thread_atexit_impl
    opendir fdopendir readdir rewinddir dirfd closedir openat fstatat unlinkat mkdirat renameat fchmodat
    __memset_chk __xuname strcasestr qsort_r arc4random arc4random_buf arc4random_uniform clock_nanosleep
    gmtime_r localtime_r times isatty pathconf getpagesizes strsignal getpwnam_r getpwuid_r umask fork
    setsid wait4 fchown sbrk syscall posix_madvise openlog popen pclose memfd_create dladdr
    access statfs fstatfs statvfs fstatvfs futimens utimensat link symlink readlink realpath mkstemp mkstemps
    posix_fallocate __assert
    accept4 getaddrinfo freeaddrinfo gai_strerror getnameinfo if_nametoindex if_indextoname if_nameindex
    if_freenameindex in6addr_any
    regcomp regexec regfree regerror open_memstream
    newlocale freelocale localeconv localeconv_l nl_langinfo nl_langinfo_l strtod_l strtof_l strtold_l strtoll_l
    strtoull_l snprintf_l sscanf_l asprintf_l strcoll_l strxfrm_l strftime_l wcscoll_l wcsxfrm_l btowc_l wctob_l
    iswctype_l mbrlen_l mbrtowc_l mbsrtowcs_l mbsnrtowcs_l wcrtomb_l wcsnrtombs_l mbtowc_l ___mb_cur_max_l
    ___runetype_l ___tolower_l ___toupper_l __runes_for_locale catopen catgets catclose backtrace backtrace_symbols_fd)
for name in "${libre_bound[@]}"; do
    for i in "${!radv_link_flags[@]}"; do
        [[ ${radv_link_flags[$i]} == "--defsym=$name=ps5_$name" ]] && radv_link_flags[$i]="--defsym=$name=libre_$name"
    done
    for i in "${!objects[@]}"; do
        [[ ${objects[$i]} == "--defsym=$name=ps5_$name" ]] && objects[$i]="--defsym=$name=libre_$name"
    done
done
# dladdr's bounds of the title's image: the libre layout's (ps5/libre/ld/ps5-title.ld);
# with PS5_Vulkan's, an empty range (dladdr then finds nothing)
[[ $libre_start == 1 ]] || radv_link_flags+=(--defsym=__libre_image_start=_start --defsym=__libre_image_end=_start)
# localeconv in the C locale, '.' the decimal point (the console's gives none, and
# tinygltf's JSON parser then read 0.62 as 0): the libre layer's (libc/locale.c).
# A recipe that binds it binds it to the platform layer's, rebound above; one that
# does not, the title binds here, kept local as the recipe keeps its bound names
if [[ " ${radv_link_flags[*]} " != *" --defsym=localeconv=libre_localeconv "* ]]; then
    printf '{\n    local:\n        localeconv;\n};\n' > "$work/link/localeconv-local.map"
    radv_link_flags+=(--defsym=localeconv=libre_localeconv --version-script "$work/link/localeconv-local.map")
fi
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$symbol_policy" --exclude-libs=ALL \
    -e _start -o "$work/link/llvm-pie.elf" \
    "$work/link/obj/app_crt.o" "${objects[@]}" \
    "$work/link/stubs/libSceAgc.so" "$work/link/stubs/libSceAgcDriver.so" \
    "${radv_link_inputs[@]}" \
    --as-needed "$sdk"/target/lib/*.so
# A title whose localeconv is still the console's reads every glTF fraction as 0
# (black materials, models scaled to nothing): refuse it here, not on the console
if grep -q " U localeconv$" <<<"$("$sdk/bin/llvm-nm" "$work/link/llvm-pie.elf" 2>/dev/null)"; then
    echo "link-title.sh: localeconv is not bound to the libre layer's libre_localeconv" >&2
    exit 1
fi
# A title loads neither libkernel_sys's exports nor libScePosixForWebKit's: an
# import only their stubs define links, and is null at run time, so its first call
# jumps to address 0 (PS5_RetroArch's strcasestr did; RADV imports readlink and
# mkstemp). PS5_Vulkan's recipe binds those the platform layer has: refuse a
# title that still imports one, here rather than on the console.
null_imports=$(comm -23 \
    <("$sdk/bin/llvm-nm" -D --undefined-only "$work/link/llvm-pie.elf" |
        awk '$1 == "U" { sub(/@.*/, "", $2); print $2 }' | sort -u) \
    <(for library in "$sdk"/target/lib/*.so "$work/link/stubs/libSceAgc.so" "$work/link/stubs/libSceAgcDriver.so"; do
        case ${library##*/} in libkernel_sys.so | libScePosixForWebKit.so) continue ;; esac
        "$sdk/bin/llvm-nm" -D --defined-only "$library" 2>/dev/null | awk '{ print $NF }'
    done | sort -u))
if [[ -n $null_imports ]]; then
    echo "link-title.sh: imports that no module a title loads exports (null at run time): ${null_imports//$'\n'/ }" >&2
    echo "link-title.sh: bind them to the platform layer (PS5_Vulkan's tools/radv-link.sh)" >&2
    exit 1
fi
"$tool" link --in "$work/link/llvm-pie.elf" --out "$work/eboot.elf.new" \
    --stub-dir "$sdk/target/lib" --stub "$work/link/stubs/libSceAgc.so" \
    --stub "$work/link/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf

# The title folder
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$param")
app="$root/dist/$title_id"
mkdir -p "$app/sce_sys" "$app/sce_module" "$app/shaders/glsl"
"$tool" self --sign --in "$work/eboot.elf.new" --out "$app/eboot.bin" --magic 0x1D3D154F
"$tool" self --inspect --file "$app/eboot.bin" > /dev/null
cp "$param" "$app/sce_sys/param.json"
for asset in icon0.png pic0.dds pic1.dds snd0.at9; do
    [[ -f $ps5/sce_sys/$asset ]] && cp "$ps5/sce_sys/$asset" "$app/sce_sys/$asset"
done
if [[ $libre_start == 1 ]]; then
    # The title's own module, written and signed here (ps5/libre/tools/make-libc-prx.py):
    # its imports, and the C library's set-up from its preinit array
    python3 "$ps5/libre/tools/make-libc-prx.py" --out "$work/link/libc.raw.elf" > /dev/null
    "$tool" self --sign --in "$work/link/libc.raw.elf" --out "$app/sce_module/libc.prx" > /dev/null
else
    (cd "$vulkan/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)
    cp "$vulkan/runtime/libc.prx" "$app/sce_module/libc.prx"
fi
# The precompiled SPIR-V of the base class and of each sample linked in
# (CMake writes the list without a final newline: read then reports end of file).
# A sample that draws only with the UI kit has none: the kit carries its own.
IFS=';' read -r -a samples < "$work/samples.txt" || (( ${#samples[@]} ))
rm -rf "$app/shaders/glsl"
mkdir -p "$app/shaders/glsl"
for dir in base "${samples[@]}"; do
    compgen -G "$root/shaders/glsl/$dir/*.spv" > /dev/null || continue
    mkdir -p "$app/shaders/glsl/$dir"
    cp "$root/shaders/glsl/$dir/"*.spv "$app/shaders/glsl/$dir/"
done
mv "$work/eboot.elf.new" "$work/eboot.elf"
archive_revision=$(sed -n 's/^revision: //p' "$(dirname "$(dirname "$archive")")/PROVENANCE.txt" 2>/dev/null || true)
printf '==> %s: %s (eboot.bin %s bytes; %d samples; RADV %s)\n' "$title_id" "$app" \
    "$(stat -c %s "$app/eboot.bin")" "${#samples[@]}" "${archive_revision:0:11}"
# The notices beside eboot.bin: LEGAL.txt and licenses/, each part with its revision
python3 "$ps5/tools/stage-notices.py" "$app" "$vulkan" "$archive"
