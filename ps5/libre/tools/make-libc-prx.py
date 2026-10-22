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
#   (0x61000047) and attributes (0x61000017), the modules and libraries it imports
#   from (0x61000045, 0x61000049, 0x61000019), the sizes of its symbol and hash
#   tables (0x6100003f, 0x6100003d);
# - symbols named NID#L#M: the NID (a hash of the function's name), the library's
#   id and the module's, each a letter of base64's alphabet. Its imports are bound
#   by the loader at load, through jump-slot relocations on zeroed slots.
#
# Its start function does what the console's C library expects of a title's
# libc.prx (with none, the title crashed in its first file operations: build 151
# with experiment 1, on my console): it registers three thread callbacks with
# libkernel (_sceKernelSetThreadDtors with one that returns,
# _sceKernelSetThreadAtexitCount with one that returns 0,
# _sceKernelSetThreadAtexitReport with one that returns), and hands libkernel's
# loader a heap table (_sceKernelRtldSetApplicationHeapAPI) with the system C
# library's malloc, free and posix_memalign (libSceLibcInternal) in its slots 0, 1
# and 6. These are the console's functions by name; the calls and the table's
# slots were read from what the current module does (its imports and its
# relocations), as facts of the console's interface.
#
# It exports two libraries, libc and libc_setjmp. Which of the console's libc
# functions it must provide is measured on the console, not copied: --exports reads
# them from a file of "NID binding type size" lines; without it the module exports
# none.
#
# The output is a raw ELF; ps5-native-tool's `self --sign` makes it the fake SELF
# the console loads.

import argparse
import base64
import hashlib
import struct

BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-"
PAGE = 0x4000
NID_SUFFIX = bytes.fromhex("518D64A635DED8C1E6B039B1C3E55230")

# Sony's program header and dynamic tags (the public PS4/PS5 module format)
PT_SCE_MODULE_PARAM = 0x61000002
PT_SCE_COMMENT = 0x6FFFFF00
DT_SCE_MODULE_INFO = 0x61000043
DT_SCE_MODULE_ATTR = 0x61000011
DT_SCE_ORIGINAL_FILENAME = 0x61000041
DT_SCE_EXPORT_LIB = 0x61000047
DT_SCE_EXPORT_LIB_ATTR = 0x61000017
DT_SCE_NEEDED_MODULE = 0x61000045
DT_SCE_IMPORT_LIB = 0x61000049
DT_SCE_IMPORT_LIB_ATTR = 0x61000019
DT_SCE_SYMTABSZ = 0x6100003F
DT_SCE_HASHSZ = 0x6100003D

DT_NULL, DT_NEEDED, DT_PLTRELSZ, DT_PLTGOT, DT_HASH, DT_STRTAB, DT_SYMTAB = 0, 1, 2, 3, 4, 5, 6
DT_RELA, DT_RELASZ, DT_RELAENT, DT_STRSZ, DT_SYMENT, DT_INIT, DT_FINI, DT_SONAME = 7, 8, 9, 10, 11, 12, 13, 14
DT_PLTREL, DT_JMPREL = 20, 23
DT_INIT_ARRAY, DT_FINI_ARRAY, DT_INIT_ARRAYSZ, DT_FINI_ARRAYSZ = 25, 26, 27, 28
DT_PREINIT_ARRAY, DT_PREINIT_ARRAYSZ = 32, 33
R_X86_64_JUMP_SLOT = 7

EXPORT_LIBRARIES = [("libc", 3), ("libc_setjmp", 4)]
# (module file, module name, module id, library name, library id)
IMPORT_FROM = [
    ("libkernel.prx", "libkernel", 1, "libkernel", 0),
    ("libSceLibcInternal.prx", "libSceLibcInternal", 2, "libSceLibcInternalExt", 1),
]
# What the start function calls and hands over: (key, function, module name)
IMPORTS = [
    ("thread_dtors", "_sceKernelSetThreadDtors", "libkernel"),
    ("atexit_count", "_sceKernelSetThreadAtexitCount", "libkernel"),
    ("atexit_report", "_sceKernelSetThreadAtexitReport", "libkernel"),
    ("heap_api", "_sceKernelRtldSetApplicationHeapAPI", "libkernel"),
    ("malloc", "malloc", "libSceLibcInternal"),
    ("free", "free", "libSceLibcInternal"),
    ("posix_memalign", "posix_memalign", "libSceLibcInternal"),
]
HEAP_SLOTS = {"malloc": 0, "free": 1, "posix_memalign": 6}
HEAP_TABLE_SIZE = 0x100

# Code addresses
INIT = 0x00
FINI = 0x80
STAND_IN = 0x90
CALLBACK_RETURN = 0xA0
CALLBACK_RETURN_ZERO = 0xB0
CODE_SIZE = 0xC0
DATA_VA = PAGE

# Data offsets
MODULE_PARAM = 0x00
PREINIT = 0x20
GOT = 0x30
GOT_RESERVED = 3
HEAP_TABLE = GOT + 8 * (GOT_RESERVED + len(IMPORTS))


def nid(name):
    digest = hashlib.sha1(name.encode() + NID_SUFFIX).digest()[:8]
    value = struct.unpack("<Q", digest)[0]
    return base64.b64encode(struct.pack(">Q", value)).decode()[:11].replace("/", "-")


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


def got_slot(key):
    index = [k for k, _, _ in IMPORTS].index(key)
    return DATA_VA + GOT + 8 * (GOT_RESERVED + index)


def code():
    out = bytearray()

    def rel32(target, instruction_length):
        return struct.pack("<i", target - (len(out) + instruction_length))

    def lea_rdi(target):
        out.extend(b"\x48\x8d\x3d" + rel32(target, 7))

    def call_slot(target):
        out.extend(b"\xff\x15" + rel32(target, 6))

    def load_rax(target):
        out.extend(b"\x48\x8b\x05" + rel32(target, 7))

    def store_rax(target):
        out.extend(b"\x48\x89\x05" + rel32(target, 7))

    # 0x00 start: the thread callbacks, then the heap table
    out.extend(b"\x48\x83\xec\x08")                  # sub rsp, 8
    lea_rdi(CALLBACK_RETURN)
    call_slot(got_slot("thread_dtors"))
    lea_rdi(CALLBACK_RETURN_ZERO)
    call_slot(got_slot("atexit_count"))
    lea_rdi(CALLBACK_RETURN)
    call_slot(got_slot("atexit_report"))
    table = DATA_VA + HEAP_TABLE
    for key, slot in HEAP_SLOTS.items():
        load_rax(got_slot(key))
        store_rax(table + 8 * slot)
    lea_rdi(table)
    call_slot(got_slot("heap_api"))
    out.extend(b"\x48\x83\xc4\x08\x31\xc0\xc3")      # add rsp, 8; xor eax, eax; ret
    assert len(out) <= FINI, len(out)

    def at(address, blob):
        out.extend(bytes(address - len(out)))
        out.extend(blob)

    at(FINI, b"\x31\xc0\xc3")                          # stop: return 0
    at(STAND_IN, b"\x66\x0f\xef\xc0\x31\xc0\xc3")      # xmm0 = 0, eax = 0, return
    at(CALLBACK_RETURN, b"\xc3")
    at(CALLBACK_RETURN_ZERO, b"\x31\xc0\xc3")
    out.extend(bytes(CODE_SIZE - len(out)))
    return bytes(out)


def read_exports(path):
    exports = []
    if not path:
        return exports
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        name, binding, kind, size = line.replace("|", " ").split()
        exports.append((name, int(binding), int(kind), int(size, 0)))
    return exports


def build(exports, comment):
    module_param = struct.pack("<QIIQQ", 0x20, 0x3C13F4BF, 3, 0x0200000908050001, 1)
    data = bytearray(HEAP_TABLE + HEAP_TABLE_SIZE)
    data[MODULE_PARAM:MODULE_PARAM + 0x20] = module_param
    object_at = {}
    for name, binding, kind, size in exports:
        if kind == 1:
            data += bytes(align(len(data), 8) - len(data))
            object_at[name] = len(data)
            data += bytes(max(size, 8))
    text = code()

    strings = bytearray(b"\0")

    def string(text_value):
        offset = len(strings)
        strings.extend(text_value.encode() + b"\0")
        return offset

    soname = string("libc.prx")
    export_names = {name: string(name) for name, _ in EXPORT_LIBRARIES}
    module_files = {file: string(file) for file, _, _, _, _ in IMPORT_FROM}
    module_names = {module: string(module) for _, module, _, _, _ in IMPORT_FROM}
    library_names = {library: string(library) for _, _, _, library, _ in IMPORT_FROM}
    original = string(comment)

    symbols = [(0, 0, 0, 0, 0, 0)]
    names = [""]
    import_index = {}
    for key, function, module in IMPORTS:
        _, _, module_id, _, library_id = next(m for m in IMPORT_FROM if m[1] == module)
        name = f"{nid(function)}#{BASE64[library_id]}#{BASE64[module_id]}"
        import_index[key] = len(symbols)
        symbols.append((string(name), (1 << 4) | 2, 0, 0, 0, 0))
        names.append(name)
    export_letter = BASE64[EXPORT_LIBRARIES[0][1]]
    for exported, binding, kind, size in exports:
        name = f"{exported}#{export_letter}#A"
        if kind == 1:
            value, section = DATA_VA + object_at[exported], 2
        else:
            value, section = STAND_IN, 1
        symbols.append((string(name), (binding << 4) | kind, 0, section, value, size))
        names.append(name)

    symtab = b"".join(struct.pack("<IBBHQQ", *s) for s in symbols)
    nchain = len(symbols)
    nbucket = max(1, nchain // 2)
    buckets = [0] * nbucket
    chains = [0] * nchain
    for index in range(1, nchain):
        h = elf_hash(names[index]) % nbucket
        chains[index] = buckets[h]
        buckets[h] = index
    hash_table = struct.pack(f"<II{nbucket}I{nchain}I", nbucket, nchain, *buckets, *chains)
    jmprel = b"".join(struct.pack("<QQq", got_slot(key), (import_index[key] << 32) | R_X86_64_JUMP_SLOT, 0)
                      for key, _, _ in IMPORTS)

    tables_va = DATA_VA + align(len(data), PAGE)
    strtab_va = tables_va
    symtab_va = align(strtab_va + len(strings), 8)
    hash_va = align(symtab_va + len(symtab), 8)
    jmprel_va = align(hash_va + len(hash_table), 8)
    dynamic_va = align(jmprel_va + len(jmprel), 8)

    dynamic = [(DT_SONAME, soname)]
    for file, module, module_id, library, library_id in IMPORT_FROM:
        dynamic += [
            (DT_NEEDED, module_files[file]),
            (DT_SCE_NEEDED_MODULE, (module_id << 48) | (1 << 40) | (1 << 32) | module_names[module]),
            (DT_SCE_IMPORT_LIB, (library_id << 48) | (1 << 32) | library_names[library]),
            (DT_SCE_IMPORT_LIB_ATTR, (library_id << 48) | 9),
        ]
    dynamic += [
        (DT_SCE_MODULE_INFO, (0 << 48) | (1 << 40) | (1 << 32) | export_names["libc"]),
        (DT_SCE_MODULE_ATTR, 0),
        (DT_SCE_ORIGINAL_FILENAME, original),
    ]
    for name, identifier in EXPORT_LIBRARIES:
        dynamic.append((DT_SCE_EXPORT_LIB, (identifier << 48) | (1 << 32) | export_names[name]))
        dynamic.append((DT_SCE_EXPORT_LIB_ATTR, (identifier << 48) | 1))
    dynamic += [
        (DT_RELA, jmprel_va), (DT_RELASZ, 0), (DT_RELAENT, 0x18),
        (DT_JMPREL, jmprel_va), (DT_PLTRELSZ, len(jmprel)), (DT_PLTGOT, DATA_VA + GOT), (DT_PLTREL, DT_RELA),
        (DT_SYMTAB, symtab_va), (DT_SYMENT, 0x18), (DT_STRTAB, strtab_va), (DT_STRSZ, len(strings)),
        (DT_HASH, hash_va),
        (DT_PREINIT_ARRAY, DATA_VA + PREINIT), (DT_PREINIT_ARRAYSZ, 8),
        (DT_INIT_ARRAY, 0), (DT_INIT_ARRAYSZ, 0), (DT_FINI_ARRAY, 0), (DT_FINI_ARRAYSZ, 0),
        (DT_INIT, INIT), (DT_FINI, FINI),
        (DT_SCE_SYMTABSZ, len(symtab)), (DT_SCE_HASHSZ, len(hash_table)),
        (DT_NULL, 0),
    ]
    dynamic_bytes = b"".join(struct.pack("<qQ", tag, value) for tag, value in dynamic)

    tables = bytearray()

    def place(va, blob):
        tables.extend(bytes(va - tables_va - len(tables)))
        tables.extend(blob)

    place(strtab_va, strings)
    place(symtab_va, symtab)
    place(hash_va, hash_table)
    place(jmprel_va, jmprel)
    place(dynamic_va, dynamic_bytes)

    comment_record = b"PATH" + struct.pack("<II", len(comment) + 1, 0) + comment.encode() + b"\0"

    phnum = 6
    code_offset = PAGE
    data_offset = code_offset + align(len(text), PAGE)
    tables_offset = data_offset + align(len(data), PAGE)
    comment_offset = align(tables_offset + len(tables), 16)
    end = comment_offset + len(comment_record)

    def phdr(kind, flags, offset, va, filesz, memsz, alignment):
        return struct.pack("<IIQQQQQQ", kind, flags, offset, va, va, filesz, memsz, alignment)

    headers = [
        phdr(1, 1, code_offset, 0, len(text), len(text), PAGE),                       # code, execute only
        phdr(1, 6, data_offset, DATA_VA, len(data), len(data), PAGE),                 # data
        phdr(PT_SCE_MODULE_PARAM, 4, data_offset + MODULE_PARAM, DATA_VA + MODULE_PARAM, 0x20, 0x20, 8),
        phdr(2, 6, tables_offset + (dynamic_va - tables_va), dynamic_va, len(dynamic_bytes), len(dynamic_bytes), 8),
        phdr(1, 0, tables_offset, tables_va, len(tables), len(tables), PAGE),         # tables, read by the loader
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
    out[code_offset:code_offset + len(text)] = text
    out[data_offset:data_offset + len(data)] = data
    out[tables_offset:tables_offset + len(tables)] = tables
    out[comment_offset:end] = comment_record
    return bytes(out), len(exports), len(IMPORTS)


def main():
    parser = argparse.ArgumentParser(description="Write the libc.prx a PS5 title carries (raw ELF; sign it with ps5-native-tool)")
    parser.add_argument("--out", required=True)
    parser.add_argument("--exports", help='"NID binding type size" lines to export (none without it)')
    parser.add_argument("--comment", default="libc.prx (PS5 libre layer, MIT)")
    arguments = parser.parse_args()
    module, exported, imported = build(read_exports(arguments.exports), arguments.comment)
    with open(arguments.out, "wb") as f:
        f.write(module)
    print(f"wrote {arguments.out}: {len(module)} bytes, {exported} exported, {imported} imported")


if __name__ == "__main__":
    main()
