#!/usr/bin/env python3
# RPCS3 for the PS5 - a readable copy of the title's code segment.
#
# The console maps a title's code execute-only, and reading it ends the title
# (SYSTEM_XO_VIOLATION). RPCS3's fault handler reads the faulting instruction to
# emulate the access; on the PS5 it reads it from this copy, which goes beside
# eboot.bin as rpcs3-code.bin (PS5_RPCS3, Utilities/Thread.cpp):
#
#   "PS5CODE1", then three little-endian u64: the link address of
#   ps5_code_copy_anchor, the code segment's link address and its size; then
#   the segment's bytes.
#
# Usage: code-copy.py <linked PIE (build/ps5/link/llvm-pie.elf)> <out>
import shutil
import struct
import subprocess
import sys

PT_LOAD = 1
PF_X = 1


def anchor_address(elf):
    for nm in ("llvm-nm", "llvm-nm-21", "llvm-nm-20", "llvm-nm-19"):
        if shutil.which(nm):
            listing = subprocess.run([nm, elf], check=True, capture_output=True, text=True).stdout
            for line in listing.splitlines():
                fields = line.split()
                if len(fields) == 3 and fields[2] == "ps5_code_copy_anchor":
                    return int(fields[0], 16)
            sys.exit(f"code-copy: no ps5_code_copy_anchor in {elf}")
    sys.exit("code-copy: no llvm-nm")


def main():
    elf, out = sys.argv[1], sys.argv[2]
    data = open(elf, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        sys.exit(f"code-copy: {elf} is not a 64-bit ELF")
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    code = []
    for i in range(phnum):
        p_type, p_flags, p_offset, p_vaddr, _, p_filesz, _, _ = struct.unpack_from("<IIQQQQQQ", data, phoff + i * phentsize)
        if p_type == PT_LOAD and p_flags & PF_X:
            code.append((p_offset, p_vaddr, p_filesz))
    if len(code) != 1:
        sys.exit(f"code-copy: {len(code)} executable segments in {elf}, one expected")
    offset, vaddr, size = code[0]
    with open(out, "wb") as f:
        f.write(b"PS5CODE1" + struct.pack("<QQQ", anchor_address(elf), vaddr, size))
        f.write(data[offset:offset + size])
    print(f"==> {out}: code segment 0x{vaddr:x}+0x{size:x}")


main()
