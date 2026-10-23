# The PS5 libre layer

What a PS5 title needs from below, written for this title under the MIT licence,
to replace the GPL-3.0 parts that keep RPCS3 for PS5 source only
([`docs/distributable-build.md`](../../docs/distributable-build.md)). Each part is
written from requirements: the C, POSIX and ELF conventions, FreeBSD's documented
behaviour, the console's exported functions by name and behaviour measured on the
console. Not from the code it replaces.

| Part | File | Replaces | Status |
| --- | --- | --- | --- |
| The entry point: FreeBSD's start block, the C library's set-up, the init and fini arrays, IEEE floating point, `main`, `catchReturnFromMain` | `crt/start.c` | PS5_Vulkan's `app_crt.cpp` and the platform's `ps5_fp_ieee` | runs on the console: build 155 with PS5_Vulkan's `libc.prx` starts, runs every step to `main` and RPCS3 |
| The memory layout: four 16 KiB-aligned segments, the unwind tables' bounds | `ld/ps5-title.ld` | PS5_Vulkan's `ps5-pie.ld` and `ps5-pie-unwind.ld` | built: the same segments, addresses and section groups as build 150's |
| The symbol policy: operator new and delete kept inside | `ld/title-symbols.map` | PS5_Vulkan's `app-symbols.map` | built |
| `sce_module/libc.prx` | `tools/make-libc-prx.py` | PS5_Vulkan's generated module | loads past the loader's checks since the build ID's note (klog); the minimal module then stops the console while it loads: being narrowed down from the console's module, which loads |

`ps5/tools/link-title.sh` takes these with `PS5_LIBRE_START=1`; the default stays
the old start-up until the new one has run on the console.
