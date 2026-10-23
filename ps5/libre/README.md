# The PS5 libre layer

What a PS5 title needs from below, written for this title under the MIT licence,
to replace the GPL-3.0 parts that keep RPCS3 for PS5 source only
([`docs/distributable-build.md`](../../docs/distributable-build.md)). Each part is
written from requirements: the C, POSIX and ELF conventions, FreeBSD's documented
behaviour, the console's exported functions by name and behaviour measured on the
console. Not from the code it replaces.

| Part | File | Replaces | Status |
| --- | --- | --- | --- |
| The entry point: FreeBSD's start block, the C library's `_init_env`, the init and fini arrays, IEEE floating point, `main`, `catchReturnFromMain` | `crt/start.c` | PS5_Vulkan's `app_crt.cpp` and the platform's `ps5_fp_ieee` | proven on the console (build 156, `PS5_LIBRE_START=1`): starts, runs to the launcher, ends through the shell |
| The memory layout: four 16 KiB-aligned segments, the unwind tables' bounds | `ld/ps5-title.ld` | PS5_Vulkan's `ps5-pie.ld` and `ps5-pie-unwind.ld` | proven on the console (build 156): the same segments, addresses and section groups as build 150's |
| The symbol policy: operator new and delete kept inside | `ld/title-symbols.map` | PS5_Vulkan's `app-symbols.map` | proven on the console (build 156) |
| `sce_module/libc.prx` | `tools/make-libc-prx.py` | PS5_Vulkan's generated module | proven on the console (build 156): its imports, the C library's set-up from its preinit array, and the build ID's note the loader requires |

`ps5/tools/link-title.sh` takes these by default since build 157 (`PS5_LIBRE_START=0`
takes PS5_Vulkan's instead). Phase 1 of
[`docs/distributable-build.md`](../../docs/distributable-build.md) is proven with
them: build 156 starts, runs to the launcher, plays God of War: Ascension and ends
through the shell, on my console.
