#!/usr/bin/env python3
"""RPCS3 for PS5 - the code from elsewhere in the title, for its notices.

    thirdparty.py BUILD_DIR      the libraries found in build/ps5's last link, and why

RPCS3 (PS5_RPCS3, GPL-2.0-only) links libraries it bundles in 3rdparty/, and the
title links RPCS3, its LLVM and the console's FFmpeg and libiconv
(rpcs3/build-*.sh). stage-notices.py lists each found here with its licence texts
and its source at the revision built; package-release.py attaches that source.

A library is in the title if the linker took a member of one of its archives
(link/why-extract.txt) or, header-only, if RPCS3's objects include its headers
(build/rpcs3's ninja dependency log): a header included may bring no code, and a
notice too many is harmless where one missing is not.

Copyright (c) 2026 KongaTime
SPDX-License-Identifier: MIT
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

PS5 = Path(__file__).resolve().parent.parent
ROOT = PS5.parent
RPCS3 = ROOT.parent / "PS5_RPCS3"
RPCS3_BUILD = ROOT / "build/rpcs3"

# id, name, licence, licence texts (paths in PS5_RPCS3, or absolute), where its
# source is (a submodule's or a fetched checkout's folder; None: in PS5_RPCS3's
# own tree), archives that show it, header folders that show it
LIBRARIES = [
    ("openal-soft", "OpenAL Soft", "LGPL-2.0-or-later", ["3rdparty/OpenAL/openal-soft/COPYING"],
     "3rdparty/OpenAL/openal-soft", ["libopenal.a"], []),
    ("soundtouch", "SoundTouch", "LGPL-2.1-or-later", ["3rdparty/SoundTouch/soundtouch/COPYING.TXT"],
     "3rdparty/SoundTouch/soundtouch", ["libsoundtouch.a"], []),
    ("libusb", "libusb", "LGPL-2.1-or-later", ["3rdparty/libusb/libusb/COPYING"],
     "3rdparty/libusb/libusb", ["libusb-1.0.a"], []),
    ("asmjit", "AsmJit", "Zlib", ["3rdparty/asmjit/asmjit/LICENSE.md"],
     "3rdparty/asmjit/asmjit", ["libasmjit.a"], []),
    ("cubeb", "cubeb", "ISC", ["3rdparty/cubeb/cubeb/LICENSE"], "3rdparty/cubeb/cubeb", ["libcubeb.a"], []),
    ("curl", "curl (libcurl)", "curl", ["3rdparty/curl/curl/COPYING"], "3rdparty/curl/curl", ["libcurl.a"], []),
    ("fusion", "Fusion (x-io Technologies)", "MIT", ["3rdparty/fusion/fusion/LICENSE.md"],
     "3rdparty/fusion/fusion", ["libFusion.a"], []),
    ("glslang", "glslang", "BSD-3-Clause, BSD-2-Clause, MIT and Apache-2.0, per file",
     ["3rdparty/glslang/glslang/LICENSE.txt"], "3rdparty/glslang/glslang", ["libglslang.a", "libSPIRV.a"], []),
    ("libpng", "libpng", "libpng-2.0", ["3rdparty/libpng/libpng/LICENSE"], "3rdparty/libpng/libpng", ["libpng16.a"], []),
    ("miniupnpc", "MiniUPnPc", "BSD-3-Clause", ["3rdparty/miniupnp/miniupnp/miniupnpc/LICENSE"],
     "3rdparty/miniupnp/miniupnp", ["libminiupnpc.a"], []),
    ("protobuf", "Protocol Buffers, with utf8_range", "BSD-3-Clause; MIT (utf8_range)",
     ["3rdparty/protobuf/protobuf/LICENSE", "3rdparty/protobuf/protobuf/third_party/utf8_range/LICENSE"],
     "3rdparty/protobuf/protobuf", ["libprotobuf.a", "libutf8_validity.a"], []),
    ("abseil", "Abseil (fetched for Protocol Buffers)", "Apache-2.0", [str(RPCS3_BUILD / "_deps/absl-src/LICENSE")],
     str(RPCS3_BUILD / "_deps/absl-src"), ["libabsl_"], []),
    ("pugixml", "pugixml", "MIT", ["3rdparty/pugixml/LICENSE.md"], "3rdparty/pugixml", ["libpugixml.a"], []),
    ("rtmidi", "RtMidi", "MIT", ["3rdparty/rtmidi/rtmidi/LICENSE"], "3rdparty/rtmidi/rtmidi", ["librtmidi.a"], []),
    ("yaml-cpp", "yaml-cpp", "MIT", ["3rdparty/yaml-cpp/yaml-cpp/LICENSE"], "3rdparty/yaml-cpp/yaml-cpp",
     ["libyaml-cpp.a"], []),
    ("zstd", "Zstandard", "BSD-3-Clause (of its BSD-3-Clause or GPL-2.0)", ["3rdparty/zstd/zstd/LICENSE"],
     "3rdparty/zstd/zstd", ["libzstd.a"], []),
    ("llvm", "LLVM (RPCS3's recompilers), with the title's patches (rpcs3/llvm-patches)",
     "Apache-2.0 WITH LLVM-exception", ["3rdparty/llvm/llvm/llvm/LICENSE.TXT"], "3rdparty/llvm/llvm", ["libLLVM"], []),
    ("vma", "Vulkan Memory Allocator", "MIT", ["3rdparty/GPUOpen/VulkanMemoryAllocator/LICENSE.txt"],
     "3rdparty/GPUOpen/VulkanMemoryAllocator", [], ["3rdparty/GPUOpen/"]),
    ("stb", "stb", "MIT (of its MIT or public domain)", ["3rdparty/stblib/stb/LICENSE"], "3rdparty/stblib/stb", [],
     ["3rdparty/stblib/"]),
    ("unordered-dense", "ankerl::unordered_dense", "MIT", ["3rdparty/unordered_dense/include/unordered_dense.h"], None,
     [], ["3rdparty/unordered_dense/"]),
    ("bcdec", "bcdec", "MIT (of its MIT or Unlicense)", ["3rdparty/bcdec/bcdec.hpp"], None, [], ["3rdparty/bcdec/"]),
    # Only its C/ files, the LZMA SDK's, each "Public domain" (3rdparty/7zip/CMakeLists.txt)
    ("7zip", "7-Zip's LZMA SDK (C/)", "public domain", ["3rdparty/7zip/7zip/DOC/lzma.txt"], "3rdparty/7zip/7zip",
     ["lib3rdparty_7zip"], ["3rdparty/7zip/"]),
]

# Not RPCS3's: built for the console by the title's scripts
FFMPEG_SRC = ROOT / ".deps/ffmpeg-src"
LIBICONV_TARBALL = ROOT / ".deps/downloads/libiconv-1.18.tar.gz"


def linked_archives(build):
    """{archive name: members taken} from the last link"""
    taken = {}
    why = Path(build) / "link/why-extract.txt"
    for line in why.read_text(errors="replace").splitlines()[1:]:
        fields = line.split("\t")
        m = re.match(r"^(.*)\((.*)\)$", fields[1]) if len(fields) > 1 else None
        if m:
            taken.setdefault(Path(m.group(1)).name, set()).add(m.group(2))
    return taken


def included_headers():
    """The 3rdparty/ paths RPCS3's objects include (build/rpcs3's ninja log)"""
    ninja = shutil.which("ninja") or str(Path.home() / ".local/bin/ninja")
    out = subprocess.run([ninja, "-C", str(RPCS3_BUILD), "-t", "deps"], capture_output=True, text=True).stdout
    prefix = str(RPCS3) + "/"
    found = set()
    for line in out.splitlines():
        path = line.strip()
        if path.startswith(prefix + "3rdparty/"):
            found.add(path[len(prefix):])
    return found


def found(build):
    """[(library tuple, evidence)] for each library of LIBRARIES in the title"""
    archives = linked_archives(build)
    headers = included_headers()
    result = []
    for library in LIBRARIES:
        _, _, _, _, _, archive_names, header_dirs = library
        evidence = []
        for name, members in archives.items():
            if any(name == a or (a.endswith("_") or not a.endswith(".a")) and name.startswith(a) for a in archive_names):
                evidence.append(f"{name}: {len(members)} members")
        for folder in header_dirs:
            count = sum(1 for h in headers if h.startswith(folder))
            if count:
                evidence.append(f"{count} headers of {folder}")
        if evidence:
            result.append((library, evidence))
    return result


def main():
    if len(sys.argv) != 2:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    for (lib_id, name, licence, *_), evidence in found(sys.argv[1]):
        print(f"{lib_id:16} {licence:60} {'; '.join(evidence)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
