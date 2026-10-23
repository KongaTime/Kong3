# RPCS3 for the PS5 - RPCS3 as the title's program (included by ps5/CMakeLists.txt).
#
# The entry (rpcs3/title_main.cpp) joins the title's objects. The link takes
# RPCS3's PS5 frontend, its emulator core and every library they were built
# with: the archives rpcs3/build-rpcs3.sh left in build/rpcs3/ (built first),
# the console's FFmpeg and libiconv (rpcs3/build-ffmpeg.sh, build-libiconv.sh), as one group, since they
# refer to each other in every direction.
#
# Copyright (C) 2026 KongaTime
# SPDX-License-Identifier: MIT

target_sources(samples PRIVATE ${ROOT}/rpcs3/title_main.cpp)
# The PS5 libre layer's memory (ps5/libre/: MIT): RPCS3's virtual memory and
# shared memory (PS5_RPCS3's rpcs3/util/vm_native_ps5.cpp), and the title's heap
# in direct memory with the malloc family the link wraps onto it. The wraps keep
# their frame pointers: the memory tracker (PS5_RPCS3's ps5_heap_track.cpp)
# reads the allocating code's return addresses through them
target_sources(samples PRIVATE ${ROOT}/ps5/libre/memory/vm.c ${ROOT}/ps5/libre/memory/heap.c ${ROOT}/ps5/libre/memory/wrap.c)
# And its threads: stacks in direct memory, thread_local destructors at a thread's end
target_sources(samples PRIVATE ${ROOT}/ps5/libre/threads/threads.c)
# And directories and the *at calls (link-title.sh binds their names)
target_sources(samples PRIVATE ${ROOT}/ps5/libre/files/dir.c ${ROOT}/ps5/libre/files/files.c)
# And the C library functions a title lacks (link-title.sh binds their names)
target_sources(samples PRIVATE ${ROOT}/ps5/libre/libc/misc.c ${ROOT}/ps5/libre/net/net.c)
# And regular expressions on musl's engine (third_party/musl-regex, MIT, unchanged:
# its own regex.h there comes first), open_memstream on the fflush and fclose
# PS5_Vulkan's recipe wraps, and standard error to klog (libre/klog.h)
set(libre_musl_regex ${ROOT}/ps5/libre/third_party/musl-regex/regcomp.c
	${ROOT}/ps5/libre/third_party/musl-regex/regexec.c ${ROOT}/ps5/libre/third_party/musl-regex/tre-mem.c)
target_sources(samples PRIVATE ${ROOT}/ps5/libre/libc/regex.c ${ROOT}/ps5/libre/libc/memstream.c
	${ROOT}/ps5/libre/libc/klog.c ${libre_musl_regex})
# And the C locale with the _l functions on it, and backtraces (link-title.sh binds them)
target_sources(samples PRIVATE ${ROOT}/ps5/libre/libc/locale.c ${ROOT}/ps5/libre/libc/backtrace.c)
set_source_files_properties(${libre_musl_regex} PROPERTIES
	INCLUDE_DIRECTORIES ${ROOT}/ps5/libre/third_party/musl-regex COMPILE_OPTIONS "-w")
set_source_files_properties(${ROOT}/ps5/libre/memory/wrap.c PROPERTIES COMPILE_OPTIONS "-fno-omit-frame-pointer")
target_include_directories(samples PRIVATE ${ROOT}/ps5/libre/include)
# stb_image's code comes once, from RPCS3 (rpcs3/Emu/stb_image.cpp, v2.30);
# the foundation's glTF loader uses it instead of its own (v2.21)
target_compile_definitions(samples PRIVATE PS5_STB_IMAGE_ELSEWHERE)
# The launch runs ps5_title_main in place of the samples (ps5/src/main.cpp)
target_compile_definitions(samples PRIVATE PS5_TITLE_MAIN)

set(rpcs3_build ${ROOT}/build/rpcs3)
if(NOT EXISTS ${rpcs3_build}/rpcs3/ps5/librpcs3_ps5.a)
	message(FATAL_ERROR "RPCS3 is not built: run rpcs3/build-rpcs3.sh rpcs3_ps5 first")
endif()

file(GLOB_RECURSE rpcs3_archives CONFIGURE_DEPENDS ${rpcs3_build}/*.a)
# RADV's archive is linked whole and carries zlib (1.3.1, Mesa's subproject):
# RPCS3's own copy (1.3.2, the same interface) would define it twice
# and FFmpeg is the console's (below), never upstream's Linux prebuilts
list(FILTER rpcs3_archives EXCLUDE REGEX "/libvulkan-placeholder\\.a$|/CMakeFiles/|/3rdparty/zlib/zlib/libz\\.a$|/3rdparty/ffmpeg/")
file(GLOB ffmpeg_archives CONFIGURE_DEPENDS ${ROOT}/.deps/native/ffmpeg-ps5/lib/*.a)
# GNU libiconv (rpcs3/build-libiconv.sh): cellL10n's text encodings
set(iconv_archive ${ROOT}/.deps/native/libiconv-ps5/lib/libiconv.a)
# LLVM's libraries for the console (rpcs3/build-llvm.sh): RPCS3's archives name
# them, and the group resolves their order
file(GLOB llvm_archives CONFIGURE_DEPENDS ${ROOT}/.deps/native/llvm-ps5/lib/libLLVM*.a)

# libc functions the console lacks, which the platform layer this title pins has
# as ps5_<name> (ps5platform/libc.h) and PS5_Vulkan's recipe does not bind yet:
# asmjit's getpagesizes, Abseil's syscall, RPCS3's times and statfs, wolfSSL's
# accept4, miniupnpc's if_nametoindex, if_indextoname, getnameinfo and
# gai_strerror, FFmpeg's isatty, libc++'s pathconf (std::filesystem; SDK fork 220b1be),
# LLVM's Support library's getpwnam_r, posix_madvise, strsignal and sbrk (SDK fork
# 1de8b37), and its process and file-system calls, which RPCS3's JIT never makes
# (fork, setsid, wait4, umask, fstatfs, fchown: refused as a title has none of
# them), and in6addr_any (RPCS3's networking): the SDK's libSceNet stub defines it, but
# the module gives a title no such export, and the shell refused to start the
# title that imported it ("can't start the game or app"); and realpath, which
# libc++'s std::filesystem canonical paths are built on: the console refuses it
# to a title (EPERM), and the package installer could not resolve its
# installation directory (the Ratchet & Clank Collection disc's PKGDIR)
set(rpcs3_libc_bindings)
foreach(name getpagesizes syscall times statfs accept4 if_nametoindex if_indextoname
		getnameinfo gai_strerror isatty pathconf getpwnam_r posix_madvise strsignal sbrk
		fork setsid wait4 umask fstatfs fchown in6addr_any realpath)
	list(APPEND rpcs3_libc_bindings --defsym=${name}=ps5_${name})
endforeach()

# Weak references nothing defines: a PIE imports them, and the native tool
# refuses an import no SDK stub exports. Each is optional, and its callers test
# its address first (if (&f) f()), so it is defined here as address 0, absolute
# and inside the program: RPCS3's thread_local variables' init functions
# (_ZTH*: other units reference them weakly; constant initialisers never define
# them), zstd's tracing hooks and gcov's. lld 20 has no -z nodynamic-undefined-weak.
# A new one names itself in the native tool's error ("no public SDK stub exports").
set(rpcs3_weak_undefined)
foreach(name
		_ZTH16g_tls_log_prefix _ZTH17g_tls_log_control _ZTH20g_tls_serialize_name
		_ZTHN10cpu_thread17g_tls_this_threadE _ZTHN10id_manager4g_idE
		_ZTHN11thread_ctrl17g_tls_this_threadE _ZTHN2fs11g_tls_errorE _ZTHN2vm12g_tls_lockedE
		_ZTHN7lv2_obj11g_to_notifyE _ZTHN7lv2_obj25g_postpone_notify_barrierE
		ZSTD_trace_compress_begin ZSTD_trace_compress_end
		ZSTD_trace_decompress_begin ZSTD_trace_decompress_end
		__gcov_dump __gcov_flush)
	list(APPEND rpcs3_weak_undefined --defsym=${name}=0)
endforeach()

# A bound name the SDK's stubs also define (isatty, statfs, pathconf...) would be
# exported from the title to override theirs, and the native tool refuses a
# title's exports: every name bound here stays local, as PS5_Vulkan's recipe
# keeps its own (tools/radv-link.sh)
set(rpcs3_local_map ${CMAKE_BINARY_DIR}/rpcs3-local.map)
set(rpcs3_local_names)
foreach(flag ${rpcs3_libc_bindings} ${rpcs3_weak_undefined})
	string(REGEX REPLACE "^--defsym=([^=]+)=.*$" "\\1" name "${flag}")
	string(APPEND rpcs3_local_names "        ${name};\n")
endforeach()
file(WRITE ${rpcs3_local_map} "{\n    local:\n${rpcs3_local_names}};\n")

# Every open by path the program makes, recorded by name (PS5_RPCS3's
# rpcs3/ps5/ps5_fdtrack.cpp): a title holds about 249 files at once, and GTA
# IV's boot ran out of them with most opened outside RPCS3's own fs
set(rpcs3_file_wraps --wrap=open --wrap=openat --wrap=fopen)

set(PS5_TITLE_LINK_INPUTS --start-group ${rpcs3_archives} ${ffmpeg_archives} ${iconv_archive} ${llvm_archives} --end-group
	${rpcs3_libc_bindings} ${rpcs3_weak_undefined} ${rpcs3_file_wraps} --version-script ${rpcs3_local_map})
set(PS5_TITLE_LINK_DEPENDS ${rpcs3_archives})
list(LENGTH rpcs3_archives rpcs3_archive_count)
message(STATUS "RPCS3: linking ${rpcs3_archive_count} archives from ${rpcs3_build}")

# The console maps the title's code execute-only and ends a title that reads it
# (SYSTEM_XO_VIOLATION). RPCS3's fault handler reads the faulting instruction, so
# the title carries a readable copy of its code segment, rpcs3-code.bin
# (rpcs3/code-copy.py; PS5_RPCS3's Utilities/Thread.cpp reads it)
function(ps5_title_post_link)
	# PS5_RPCS3's checkout, found as rpcs3/build-rpcs3.sh finds it
	set(rpcs3_src "$ENV{RPCS3_SRC}")
	if(NOT rpcs3_src)
		foreach(candidate ${ROOT}/../PS5_RPCS3 ${ROOT}/../ps5_rpcs3)
			if(EXISTS ${candidate}/rpcs3/CMakeLists.txt)
				set(rpcs3_src ${candidate})
				break()
			endif()
		endforeach()
	endif()
	file(READ ${ROOT}/ps5/sce_sys/param.json rpcs3_param)
	string(JSON rpcs3_title_id GET "${rpcs3_param}" titleId)
	# And RPCS3's overlay images (bin/Icons/ui: the pad's buttons, the save
	# list's "new" entry, the spinner), which its native dialogs load from
	# /app0/rpcs3/Icons/ui/; RPCS3's own, under its licence
	add_custom_command(TARGET title POST_BUILD
		COMMAND python3 ${ROOT}/rpcs3/code-copy.py ${CMAKE_BINARY_DIR}/link/llvm-pie.elf ${ROOT}/dist/${rpcs3_title_id}/rpcs3-code.bin
		COMMAND ${CMAKE_COMMAND} -E copy_directory ${rpcs3_src}/bin/Icons/ui ${ROOT}/dist/${rpcs3_title_id}/rpcs3/Icons/ui
		VERBATIM)
endfunction()
