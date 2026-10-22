#!/usr/bin/env python3
# RPCS3 for PS5 - writes the libc.prx module a PS5 title carries in sce_module/.
# Copyright (c) 2026 KongaTime
# SPDX-License-Identifier: MIT
#
# Part of the PS5 libre layer (ps5/libre/README.md). The console's loader will not
# start a title without its own sce_module/libc.prx (0xa0020102,
# prx_sce_module_load_error, when it is missing: measured on my console with build
# 151), and the console's modules link against it. This writes one from what a
# PS5 module is, as the console's loader reads it:
#
# - an ELF of type 0xfe18 (Sony's dynamic module), OS/ABI FreeBSD, ABI version 2;
# - its code in an execute-only segment, its data in a writable one, each 16 KiB
#   aligned, and a module-parameter block (size 0x20, magic 0x3c13f4bf) in the data
#   with a program header of its own (0x61000002);
# - its dynamic tables (names, symbols, a SysV hash, relocations, the dynamic
#   section) in a segment the loader reads but does not map (flags 0), with the
#   standard tags and Sony's: the module's name and version (0x61000043), its
#   attributes (0x61000011), the libraries it exports with their versions
#   (0x61000047) and attributes (0x61000017), the sizes of its symbol and hash
#   tables (0x6100003f, 0x6100003d);
# - exported symbols named NID#L#M: the NID (a hash of the function's name), the
#   library's id and the module's, each in base64's alphabet.
#
# The module exports two libraries, libc and libc_setjmp. Which of the console's
# libc functions it must provide is measured on the console, not copied: --exports
# reads them from a file of "NID binding type size" lines; without it the module
# exports none (the first experiment).
#
# The output is a raw ELF; ps5-native-tool's `self --sign` makes it the fake SELF
# the console loads (ps5/tools/link-title.sh).

import argparse
import struct

BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-"
PAGE = 0x4000

# Sony's program header and dynamic tags (the public PS4/PS5 module format)
PT_SCE_MODULE_PARAM = 0x61000002
PT_SCE_COMMENT = 0x6FFFFF00
DT_SCE_MODULE_INFO = 0x61000043
DT_SCE_MODULE_ATTR = 0x61000011
DT_SCE_ORIGINAL_FILENAME = 0x61000041
DT_SCE_EXPORT_LIB = 0x61000047
DT_SCE_EXPORT_LIB_ATTR = 0x61000017
DT_SCE_SYMTABSZ = 0x6100003F
DT_SCE_HASHSZ = 0x6100003D

DT_NULL, DT_HASH, DT_STRTAB, DT_SYMTAB, DT_RELA, DT_RELASZ, DT_RELAENT, DT_STRSZ, DT_SYMENT = 0, 4, 5, 6, 7, 8, 9, 10, 11
DT_INIT, DT_FINI, DT_SONAME = 12, 13, 14
DT_INIT_ARRAY, DT_FINI_ARRAY, DT_INIT_ARRAYSZ, DT_FINI_ARRAYSZ = 25, 26, 27, 28
DT_PREINIT_ARRAY, DT_PREINIT_ARRAYSZ = 32, 33

# The module's own code: start, stop, and the stand-in every exported function
# points at, which returns zero in rax and xmm0
CODE = bytes.fromhex(
    "31c0c3" + "00" * 13 +          # 0x00 start: return 0
    "31c0c3" + "00" * 13 +          # 0x10 stop: return 0
    "660fefc031c0c3" + "00" * 9     # 0x20 stand-in: xmm0 = 0, eax = 0, return
)
INIT, FINI, STAND_IN = 0x00, 0x10, 0x20

LIBRARIES = [("libc", 3), ("libc_setjmp", 4)]


def align(value, to):
    return (value + to - 1) // to * to


def elf_hash(name):
    h = 0
    for c in name.encode():
        h = ((h << 4) + c) & 0xFFFFFFFF
        g = h & 0xF0000000
        if g:
            h ^= g >> 24
        h &= ~g & 0xFFFFFFFF
    return h


def read_exports(path):
    exports = []
    if not path:
        return exports
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        nid, binding, kind, size = line.replace("|", " ").split()
        exports.append((nid, int(binding), int(kind), int(size, 0)))
    return exports


def build(exports, comment):
    # The writable data: the module-parameter block and an empty preinit array
    module_param = struct.pack("<QIIQQ", 0x20, 0x3C13F4BF, 3, 0x0200000908050001, 1)[:0x20]
    data = bytearray(module_param + bytes(8))
    preinit_offset = 0x20
    # Each exported object gets zeroed room of its advertised size in the data
    object_at = {}
    for nid, binding, kind, size in exports:
        if kind == 1:
            data += bytes(align(len(data), 8) - len(data))
            object_at[nid] = len(data)
            data += bytes(max(size, 8))

    code_va = 0
    data_va = align(len(CODE), PAGE)

    # The dynamic tables: names, symbols, hash, the dynamic section
    strings = bytearray(b"\0")
    def string(text):
        offset = len(strings)
        strings.extend(text.encode() + b"\0")
        return offset

    soname = string("libc.prx")
    library_names = {name: string(name) for name, _ in LIBRARIES}
    original = string(comment)

    symbols = [(0, 0, 0, 0, 0, 0)]
    names = [""]
    library_letter = BASE64[LIBRARIES[0][1]]
    for nid, binding, kind, size in exports:
        name = f"{nid}#{library_letter}#A"
        if kind == 1:
            value, section = data_va + object_at[nid], 2
        else:
            value, section = code_va + STAND_IN, 1
        symbols.append((string(name), (binding << 4) | kind, 0, section, value, size))
        names.append(name)

    symtab = b"".join(struct.pack("<IBBHQQ", *s) for s in symbols)
    nchain = len(symbols)
    nbucket = max(1, nchain // 2) if nchain > 1 else 1
    buckets = [0] * nbucket
    chains = [0] * nchain
    for index in range(1, nchain):
        h = elf_hash(names[index]) % nbucket
        chains[index] = buckets[h]
        buckets[h] = index
    hash_table = struct.pack(f"<II{nbucket}I{nchain}I", nbucket, nchain, *buckets, *chains)

    # Layout of the unmapped segment: strings, symbols, hash, then the dynamic section
    tables_va = data_va + align(len(data), PAGE)
    strtab_va = tables_va
    symtab_va = align(strtab_va + len(strings) + 0, 8)
    hash_va = align(symtab_va + len(symtab), 8)
    rela_va = align(hash_va + len(hash_table), 8)
    dynamic_va = rela_va

    def library_value(identifier, version, name_offset):
        return (identifier << 48) | (version << 32) | name_offset

    dynamic = [
        (DT_SONAME, soname),
        (DT_SCE_MODULE_INFO, (0 << 48) | (1 << 40) | (1 << 32) | soname_module(strings, library_names)),
        (DT_SCE_MODULE_ATTR, 0),
        (DT_SCE_ORIGINAL_FILENAME, original),
    ]
    for name, identifier in LIBRARIES:
        dynamic.append((DT_SCE_EXPORT_LIB, library_value(identifier, 1, library_names[name])))
        dynamic.append((DT_SCE_EXPORT_LIB_ATTR, (identifier << 48) | 1))
    dynamic += [
        (DT_RELA, rela_va), (DT_RELASZ, 0), (DT_RELAENT, 0x18),
        (DT_SYMTAB, symtab_va), (DT_SYMENT, 0x18), (DT_STRTAB, strtab_va), (DT_STRSZ, len(strings)),
        (DT_HASH, hash_va),
        (DT_PREINIT_ARRAY, data_va + preinit_offset), (DT_PREINIT_ARRAYSZ, 8),
        (DT_INIT_ARRAY, 0), (DT_INIT_ARRAYSZ, 0), (DT_FINI_ARRAY, 0), (DT_FINI_ARRAYSZ, 0),
        (DT_INIT, code_va + INIT), (DT_FINI, code_va + FINI),
        (DT_SCE_SYMTABSZ, len(symtab)), (DT_SCE_HASHSZ, len(hash_table)),
        (DT_NULL, 0),
    ]
    dynamic_bytes = b"".join(struct.pack("<qQ", tag, value) for tag, value in dynamic)

    tables = bytearray()
    def place(va, blob):
        offset = va - tables_va
        tables.extend(bytes(offset - len(tables)))
        tables.extend(blob)
    place(strtab_va, strings)
    place(symtab_va, symtab)
    place(hash_va, hash_table)
    place(dynamic_va, dynamic_bytes)

    comment_record = b"PATH" + struct.pack("<II", len(comment) + 1, 0) + comment.encode() + b"\0"

    # File layout
    phnum = 6
    code_offset = PAGE
    data_offset = code_offset + align(len(CODE), PAGE)
    tables_offset = data_offset + align(len(data), PAGE)
    comment_offset = align(tables_offset + len(tables), 16)
    end = comment_offset + len(comment_record)

    def phdr(kind, flags, offset, va, filesz, memsz, alignment):
        return struct.pack("<IIQQQQQQ", kind, flags, offset, va, va, filesz, memsz, alignment)

    headers = [
        phdr(1, 1, code_offset, code_va, len(CODE), len(CODE), PAGE),                  # code, execute only
        phdr(1, 6, data_offset, data_va, len(data), len(data), PAGE),                  # data
        phdr(PT_SCE_MODULE_PARAM, 4, data_offset, data_va, 0x20, 0x20, 8),
        phdr(2, 6, tables_offset + (dynamic_va - tables_va), dynamic_va, len(dynamic_bytes), len(dynamic_bytes), 8),
        phdr(1, 0, tables_offset, tables_va, len(tables), len(tables), PAGE),          # tables, read by the loader
        phdr(PT_SCE_COMMENT, 0, comment_offset, 0, len(comment_record), 0, 16),
    ]
    elf_header = struct.pack(
        "<16sHHIQQQIHHHHHH",
        b"\x7fELF\x02\x01\x01\x09\x02" + bytes(7),
        0xFE18, 0x3E, 1, 0, 64, 0, 0, 64, 56, phnum, 64, 0, 0,
    )
    out = bytearray(end)
    out[0:64] = elf_header
    out[64:64 + 56 * phnum] = b"".join(headers)
    out[code_offset:code_offset + len(CODE)] = CODE
    out[data_offset:data_offset + len(data)] = data
    out[tables_offset:tables_offset + len(tables)] = tables
    out[comment_offset:end] = comment_record
    return bytes(out), len(symbols) - 1


def soname_module(strings, library_names):
    # The module's name is its first library's: libc
    return library_names["libc"]


def main():
    parser = argparse.ArgumentParser(description="Write the libc.prx a PS5 title carries (raw ELF; sign it with ps5-native-tool)")
    parser.add_argument("--out", required=True)
    parser.add_argument("--exports", help='"NID binding type size" lines to export (none without it)')
    parser.add_argument("--comment", default="libc.prx (PS5 libre layer, MIT)")
    arguments = parser.parse_args()
    module, count = build(read_exports(arguments.exports), arguments.comment)
    with open(arguments.out, "wb") as f:
        f.write(module)
    print(f"wrote {arguments.out}: {len(module)} bytes, {count} exported symbols")


if __name__ == "__main__":
    main()
