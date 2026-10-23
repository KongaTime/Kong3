#!/usr/bin/env python3
"""RPCS3 for PS5 - what in a linked title is GPL-3.0-or-later.

    shareable.py BUILD_DIR            the verdict on build/ps5's last link: 0 if
                                      nothing GPL-3.0 was found, 1 if something was

A title with GPL-3.0 code in it cannot be shared (docs/distributable-build.md).
This reads what the link left (link-title.sh) and names each GPL-3.0 part in the
title, with what shows it:

  platform     a member of the PS5 platform layer (libps5platform.a) the linker
               took (link/why-extract.txt, lld's --why-extract)
  ps5-vulkan   PS5_Vulkan's CRT, linker script, symbol policy or libc.prx in the
               link (link/parts.txt); or code its native tool added in packaging
               eboot.bin and libc.prx (it adds none: packaging_findings)
  lsfg         Lossless Scaling's frame generation (PS5_RPCS3's RSX/VK/lsfg/)
  mihawkgen    MihawkGen's (PS5_RPCS3's RSX/VK/mihawkgen/, VKFrameGenMihawk.cpp)
  ui-kit       the UI kit (ps5/ui/), in a title that has it
  wolfssl      wolfSSL (RPCS3's 3rdparty/wolfssl), RPCN's TLS and libcurl's

the frame generators by their objects in the link or their symbols in the ELF
(PS5_RPCS3's PS5_GPL3_FRAME_GENERATION builds them in). stage-notices.py lists
the parts found; package-release.py --shareable refuses a title with any.

Copyright (c) 2026 KongaTime
SPDX-License-Identifier: MIT
"""

import json
import re
import struct
import subprocess
import sys
from pathlib import Path

PS5 = Path(__file__).resolve().parent.parent

# Archive members of the frame generators (PS5_RPCS3's rpcs3/Emu/CMakeLists.txt)
FRAME_GENERATOR_MEMBERS = [
    (re.compile(r"^(lsfg_\w+|lossless_dll|vulkan_adapter)\.cpp\.o$"), "lsfg"),
    (re.compile(r"^(fg_common|fg_neural|fg_splat|fg_vulkan|VKFrameGenMihawk)\.cpp\.o$"), "mihawkgen"),
]
# Libraries RPCS3 bundles that are GPL-3.0-or-later: wolfSSL (3rdparty/wolfssl,
# from 5.7: GPL-3.0, GPL-2.0 only for the projects its LICENSING names), RPCN's
# TLS and libcurl's
GPL3_ARCHIVES = {"libwolfssl.a": "wolfssl"}
# Symbols only their code defines: the Lossless Scaling port's namespace
# (rex::ui::vulkan::framegen), MihawkGen's fg_* functions and its network
SYMBOLS = [
    (re.compile(r"^_ZN\w*3rex2ui6vulkan8framegen"), "lsfg"),
    (re.compile(r"^(_Z\d+fg_\w+|ps5_mihawkgen_network\w*)$"), "mihawkgen"),
]

NAMES = {
    "platform": "the PS5 platform layer (libps5platform.a)",
    "ps5-vulkan": "PS5_Vulkan",
    "lsfg": "Lossless Scaling's frame generation (PS5_RPCS3's RSX/VK/lsfg/)",
    "mihawkgen": "MihawkGen's frame generation (PS5_RPCS3's RSX/VK/mihawkgen/)",
    "ui-kit": "the UI kit (PS5_VKHomebrewUI, ps5/ui/)",
    "wolfssl": "wolfSSL (PS5_RPCS3's 3rdparty/wolfssl): RPCN's TLS and libcurl's",
}


def program_headers(data):
    """An ELF64's program headers: (type, flags, offset, vaddr, filesz), and the
    bytes of the ELF header and the table."""
    if data[:4] != b"\x7fELF":
        return None, b""
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    headers = []
    for i in range(phnum):
        p_type, p_flags, p_offset, p_vaddr, _, p_filesz, _, _ = struct.unpack_from("<IIQQQQQQ", data, phoff + i * phentsize)
        headers.append((p_type, p_flags, p_offset, p_vaddr, p_filesz))
    return headers, data[:0x40] + data[phoff:phoff + phentsize * phnum]


def executable(headers, data):
    """{vaddr: bytes} of each executable PT_LOAD"""
    return {va: data[off:off + size] for kind, flags, off, va, size in headers if kind == 1 and flags & 1}


def packaging_findings(build):
    """What PS5_Vulkan's native tool added to the title's code, if anything.

    Measured for build 171 (docs/distributable-build.md): its link step leaves
    the code and read-only data byte for byte and adds the console's tables
    (the symbols' NIDs, the imports, the process parameters), unmapped or data;
    its SELF wrapping copies the ELF verbatim behind a header, zeros and a
    digest. Checked again at each link: every executable segment it writes is
    one of the input's, byte for byte, and reaches the signed file verbatim."""
    reasons = []
    title_id = json.loads((PS5 / "sce_sys/param.json").read_text())["titleId"]
    app = PS5.parent / "dist" / title_id
    pairs = [("eboot.bin", build / "link/llvm-pie.elf", build / "eboot.elf", app / "eboot.bin"),
             ("sce_module/libc.prx", build / "link/libc.raw.elf", None, app / "sce_module/libc.prx")]
    for name, linked, converted, signed in pairs:
        if not linked.exists() or not signed.exists():
            reasons.append(f"{name}: cannot check its packaging ({linked.name} or {signed.name} is missing)")
            continue
        source = linked.read_bytes()
        source_headers, _ = program_headers(source)
        source_code = executable(source_headers, source)
        if converted is not None:
            data = converted.read_bytes()
            headers, _ = program_headers(data)
            code = executable(headers, data)
            if code != source_code:
                reasons.append(f"{name}: its link step changed or added executable bytes "
                               f"({len(code)} executable segments out, {len(source_code)} in)")
                continue
        else:
            data, headers, code = source, source_headers, source_code
        wrapped = signed.read_bytes()
        _, header_bytes = program_headers(data)
        if wrapped.find(header_bytes) < 0:
            reasons.append(f"{name}: the signed file does not carry the ELF's headers verbatim")
        for va, segment in code.items():
            if wrapped.find(segment) < 0:
                reasons.append(f"{name}: the signed file does not carry the code at {va:#x} verbatim")
    return reasons


def findings(build, nm="llvm-nm"):
    """{part: [what shows it]} for each GPL-3.0 part in the title build/ps5 last linked."""
    link = Path(build) / "link"
    found = {}

    def add(part, reason):
        found.setdefault(part, [])
        if reason not in found[part]:
            found[part].append(reason)

    # The archive members the linker took
    why = link / "why-extract.txt"
    if not why.exists():
        add("unknown", f"{why} is missing: link again with this link-title.sh")
    else:
        for line in why.read_text(errors="replace").splitlines()[1:]:
            fields = line.split("\t")
            if len(fields) < 2:
                continue
            member = re.match(r"^(.*)\((.*)\)$", fields[1])
            if not member:
                continue
            archive, name = Path(member.group(1)).name, member.group(2)
            if archive == "libps5platform.a":
                add("platform", f"{name} (for {fields[2] if len(fields) > 2 else '?'})")
            if archive in GPL3_ARCHIVES:
                add(GPL3_ARCHIVES[archive], f"{archive}({name})")
            for pattern, part in FRAME_GENERATOR_MEMBERS:
                if pattern.match(name):
                    add(part, f"{archive}({name})")

    # The CRT, layout, symbol policy and libc.prx the link took
    parts = link / "parts.txt"
    if parts.exists():
        for line in parts.read_text().splitlines():
            kind, _, source = line.partition(" ")
            if source and not source.startswith(str(PS5 / "libre")):
                add("ps5-vulkan", f"{kind}: {source}")
    else:
        add("unknown", f"{parts} is missing: link again with this link-title.sh")
    # Its native tool packages eboot.bin and libc.prx: it must add no code
    for reason in packaging_findings(Path(build)):
        add("ps5-vulkan", reason)

    # The symbols the ELF defines
    elf = link / "llvm-pie.elf"
    listing = subprocess.run([nm, "--defined-only", str(elf)], capture_output=True, text=True, check=True).stdout
    counts = {}
    for line in listing.splitlines():
        name = line.split()[-1]
        for pattern, part in SYMBOLS:
            if pattern.match(name):
                counts[part] = counts.get(part, 0) + 1
    for part, count in counts.items():
        add(part, f"{count} of its symbols in the ELF")

    if (PS5 / "ui/setup-kit.sh").exists():
        add("ui-kit", "ps5/ui/ is in this title")
    return found


def main():
    if len(sys.argv) != 2:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    sdk_nm = PS5.parent / ".deps/native/ps5-payload-sdk/bin/llvm-nm"
    found = findings(sys.argv[1], str(sdk_nm) if sdk_nm.exists() else "llvm-nm")
    if not found:
        print("shareable: nothing GPL-3.0-or-later in the title")
        return 0
    print(f"shareable: no, {len(found)} GPL-3.0-or-later part{'s' if len(found) > 1 else ''} in the title:")
    for part, reasons in found.items():
        print(f"  {NAMES.get(part, part)}:")
        for reason in reasons:
            print(f"    {reason}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
