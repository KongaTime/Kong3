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
| Virtual memory: address ranges, the memory committed in them, shared memory | `memory/vm.c` (`include/libre/vm.h`) | the platform's `shm.o` | proven on the console (build 158): GTA IV and Web of Shadows played and left |
| The heap in direct memory: dlmalloc's mspaces (MIT-0, `third_party/dlmalloc`, 32-byte blocks) in a 64 GiB range, a few arenas, statistics | `memory/heap.c` (`include/libre/heap.h`) | the platform's `heap.o` and its `mspace.o` | proven on the console (build 160): GTA IV, the heap flat at 2262 MiB |
| The malloc family the link wraps (malloc to getline), with a tracker interface | `memory/wrap.c` | the platform's `heap_wrap.o` and PS5_RPCS3's `ps5_alloc_track.cpp` wraps | proven on the console (build 160) |
| Threads: stacks of 2 MiB or more in direct memory with a guard, given back at join or (detached) after the end; thread_local destructors at a thread's end (`__cxa_thread_atexit_impl`) | `threads/threads.c` (`include/libre/threads.h`) | the platform's `threads.o`, `cxa.o` and `placement.o` | proven on the console (build 161): GTA V played and left, then Web of Shadows |
| Directories (`opendir` to `closedir`, `dirfd`) and the `*at` calls (`openat`, `fstatat`, `unlinkat`, `mkdirat`, `renameat`, `fchmodat`), on the path calls | `files/dir.c` | the platform's `directory.o` (still linked for `libc.o` until it goes) | proven on the console (build 162): 12 games listed, a save written, listed and deleted with its backup |
| C library functions a title lacks, the small ones: randomness (`arc4random*`, the kernel's `kern.arandom`), `gmtime_r`, `localtime_r`, `clock_nanosleep`, `times`, `uname`, `getpagesizes`, `pathconf`, `strsignal`, `dladdr` (the title's own image), `qsort_r`, `strcasestr`, `__memset_chk`, and refusals for what a title cannot do (`fork`, `wait4`, `setsid`, `fchown`, `sbrk`, `syscall` but write, `popen`, `memfd_create`) | `libc/misc.c` | 30 of the platform's `libc.o` and `posix.o` | proven on the console (build 163): Web of Shadows played and left |
| Network calls a title lacks: `getaddrinfo`/`getnameinfo` for numeric addresses and `localhost`, `freeaddrinfo`, `gai_strerror`, `accept4`, `in6addr_any`, the `if_*` calls (no interface by name); threads' CPUs (`libre_pthread_{get,set}affinity_np`, any size of set, on the console's 64-bit masks) | `net/net.c`, `threads/threads.c` | the rest of the platform's `libc.o`, which the title no longer links | proven on the console (build 165): Ratchet & Clank Collection and Web of Shadows played at 60 frames a second |
| Regular expressions (`regcomp`, `regexec`, `regfree`, `regerror`): FreeBSD's interface, with `REG_PEND`, `REG_NOSPEC` and `REG_STARTEND`, on musl 1.2.5's engine (MIT, `third_party/musl-regex`, unchanged) | `libc/regex.c` | the platform's `regex.o`, `regcomp.o`, `regexec.o` and `tre-mem.o` | proven on the console (build 168): the start's check matched, groups and `REG_STARTEND` |
| `open_memstream` on a temporary file (`/download0` or `/app0`: a title has no `/tmp` or `/data`, and its anonymous shared memory takes no write past its end), published at the `fflush` and `fclose` the link wraps | `libc/memstream.c` | the platform's `memstream.o` | proven on the console (build 168) |
| Standard error to klog: `stderr` a stream on a pipe, whose lines a thread hands to `sceKernelDebugOutText` after a prefix (descriptor 2 cannot be moved) | `libc/klog.c` (`include/libre/klog.h`) | the platform's `klog.o` | proven on the console (build 168): 3305 lines of a Web of Shadows run in klog |
| The C locale and the `_l` functions on it (`newlocale` to `wcsnrtombs_l`, 37 in all, with the rune table libc++'s inline classifiers read, built from the console's own `isw*` and `tow*`), `localeconv` and `nl_langinfo` (`.` the decimal point), the encoding the console's conversions use (UTF-8, though its `MB_CUR_MAX` says 1), no message catalogs; `backtrace` and `backtrace_symbols_fd` on LLVM's libunwind | `libc/locale.c`, `libc/backtrace.c` | the platform's `xlocale.o` and `locale.o`, the last of it the title linked | proven on the console (build 170): libc++'s numbers, streams and ctype, Web of Shadows played |
| File calls a title lacks or is refused: `access`, `statfs`/`fstatfs`/`statvfs`/`fstatvfs` (fixed figures: the console refuses a title `_fstatfs`), `futimens`/`utimensat`, `link`/`symlink`/`readlink` (no links), `realpath`, `mkstemp`/`mkstemps`, `posix_fallocate`; and `__assert` | `files/files.c`, `libc/misc.c` | the rest of the platform's `posix.o`, which the title no longer links, and 9 of its `libc.o` | proven on the console (build 164): Ratchet & Clank Collection booted, saved and left |

`ps5/tools/link-title.sh` takes these by default since build 157 (`PS5_LIBRE_START=0`
takes PS5_Vulkan's instead). Phase 1 of
[`docs/distributable-build.md`](../../docs/distributable-build.md) is proven with
them: build 156 starts, runs to the launcher, plays God of War: Ascension and ends
through the shell, on my console.

Since build 170 the title links nothing of the platform layer (`libps5platform.a`):
phase 3 of [`docs/distributable-build.md`](../../docs/distributable-build.md) is done.
