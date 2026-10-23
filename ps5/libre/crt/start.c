/*
 * RPCS3 for PS5 - the title's entry point.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md): written for this title from
 * the ELF start-up conventions and FreeBSD's process start ABI, which the PS5's
 * loader follows, not from another start-up file.
 *
 * The loader calls _start with a pointer to the process's start block and a
 * function to run at exit. The block is FreeBSD's: argc in its first 8-byte slot,
 * then argv (argc pointers and a null), then the environment (pointers and a
 * null). The console's C library sets itself up from the same block
 * (_init_env). Then the start-up runs what the compiler and linker gathered:
 * the preinit and init arrays in order, the fini array at exit in reverse.
 */

#include <stdint.h>

typedef void (*start_function)(void);

/* The console's C library */
extern void _init_env(void *start_block);
extern int atexit(void (*function)(void));
extern void exit(int status) __attribute__((noreturn));

/* The program */
extern int main(int argc, char **argv, char **envp);

/* Bounds the linker defines (ps5/libre/ld/ps5-title.ld); empty arrays allowed */
extern start_function __preinit_array_start[] __attribute__((weak, visibility("hidden")));
extern start_function __preinit_array_end[] __attribute__((weak, visibility("hidden")));
extern start_function __init_array_start[] __attribute__((weak, visibility("hidden")));
extern start_function __init_array_end[] __attribute__((weak, visibility("hidden")));
extern start_function __fini_array_start[] __attribute__((weak, visibility("hidden")));
extern start_function __fini_array_end[] __attribute__((weak, visibility("hidden")));

/* This module's handle for __cxa_atexit and __cxa_thread_atexit, as crtbegin defines it */
void *__dso_handle __attribute__((visibility("hidden"))) = &__dso_handle;

/* Called with main's result. The template's PS5 layer defines its own, which asks
 * the shell to close the title (a title never calls exit: ps5/src/platform.c);
 * this one only stands in where nothing else does */
__attribute__((weak, noreturn)) void catchReturnFromMain(int status)
{
   exit(status);
}

/* Kept for anything that still names them: nothing to do */
void _init(void)
{
}

void _fini(void)
{
}

static void run_fini_array(void)
{
   if (!__fini_array_start || !__fini_array_end)
      return;
   for (start_function *at = __fini_array_end; at != __fini_array_start;)
   {
      --at;
      if (*at)
         (*at)();
   }
}

static void run_array(start_function *first, start_function *last)
{
   if (!first || !last)
      return;
   for (start_function *at = first; at != last; ++at)
   {
      if (*at)
         (*at)();
   }
}

/* IEEE floating point: a title starts with MXCSR 0x9fe0, flushing denormals to
 * zero in both directions (measured on the console). 0x1f80 is the x86-64
 * default: every exception masked, round to nearest, no flushing. The x87
 * control word gets FreeBSD's amd64 default, 0x037f (exceptions masked, round
 * to nearest, 64-bit precision) */
static void floating_point_ieee(void)
{
   const uint32_t mxcsr = 0x1f80;
   const uint16_t x87 = 0x037f;
   __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
   __asm__ volatile("fldcw %0" : : "m"(x87));
}

__attribute__((noreturn, visibility("default"))) void _start(void *start_block, void (*at_exit)(void))
{
   uint64_t *slots = (uint64_t *)start_block;
   const int argc = start_block ? (int)slots[0] : 0;
   char **argv = start_block ? (char **)(slots + 1) : 0;
   char **envp = argv ? argv + argc + 1 : 0;

   floating_point_ieee();
   _init_env(start_block);
   if (at_exit)
      (void)atexit(at_exit);
   (void)atexit(run_fini_array);

   run_array(__preinit_array_start, __preinit_array_end);
   run_array(__init_array_start, __init_array_end);

   catchReturnFromMain(main(argc, argv, envp));
}
