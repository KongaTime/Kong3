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

/* What a title's libc.prx does at its start, done here first instead, so the
 * module the title carries needs no imports (ps5/libre/tools/make-libc-prx.py
 * --minimal): with imports, even unused, the console stopped before the title's
 * first line (experiments 2 to 6 on my console); with none and no set-up, the
 * title ran until its first file operations (experiment 1). The set-up: three
 * thread callbacks for libkernel, and libkernel's loader handed a heap table with
 * the system C library's malloc, free and posix_memalign in slots 0, 1 and 6. The
 * title wraps malloc and the others (its heap in direct memory), so the system's
 * own are the __real_ names */
/* Looked up at run time, not imported: a title importing them was refused by the
 * loader before its first instruction (builds 152 and 153 on my console; no
 * /app0/libre-start.txt), as was a libc.prx importing them (experiments 2 to 6) */
typedef int (*callback_setter)(void *callback);
typedef int (*heap_api_setter)(void *table[]);
extern int sceKernelGetModuleList(int *handles, unsigned long capacity, unsigned long *count);
extern int sceKernelGetModuleInfo(int handle, void *info);
extern int sceKernelDlsym(int handle, const char *name, void **address);
extern void *__real_malloc(unsigned long size);
extern void __real_free(void *pointer);
extern int __real_posix_memalign(void **out, unsigned long alignment, unsigned long size);

static void *heap_api[32];

/* Diagnostics for the start-up, until it is proven on the console: a line in
 * /app0/libre-start.txt for each step, written with libkernel's own file calls
 * (the C library is not set up yet), and switch files that leave a step out.
 * FreeBSD's O_WRONLY | O_APPEND | O_CREAT */
extern int sceKernelOpen(const char *path, int flags, int mode);
extern long sceKernelWrite(int fd, const void *buffer, unsigned long size);
extern int sceKernelClose(int fd);
extern int sceKernelStat(const char *path, void *status);

static void step(const char *text)
{
   unsigned long length = 0;
   while (text[length])
      length++;
   const int fd = sceKernelOpen("/app0/libre-start.txt", 0x1 | 0x8 | 0x200, 0666);
   if (fd < 0)
      return;
   (void)sceKernelWrite(fd, text, length);
   (void)sceKernelWrite(fd, "\n", 1);
   (void)sceKernelClose(fd);
}

static int switched_off(const char *path)
{
   unsigned char status[256];
   return sceKernelStat(path, status) == 0;
}

static void thread_callback(void)
{
}

static int thread_atexit_count(void)
{
   return 0;
}

/* The module information sceKernelGetModuleInfo fills: its size first, then the
 * module's name, then segments and a fingerprint (0x160 bytes in all) */
struct module_info
{
   unsigned long size;
   char name[256];
   unsigned char rest[0x160 - 8 - 256];
};

static int starts_with(const char *text, const char *prefix)
{
   while (*prefix)
   {
      if (*text++ != *prefix++)
         return 0;
   }
   return 1;
}

static int libkernel_handle(void)
{
   int handles[256];
   unsigned long count = 0;
   if (sceKernelGetModuleList(handles, 256, &count) != 0)
      return -1;
   for (unsigned long i = 0; i < count && i < 256; i++)
   {
      struct module_info info;
      for (unsigned long b = 0; b < sizeof(info); b++)
         ((unsigned char *)&info)[b] = 0;
      info.size = sizeof(info);
      if (sceKernelGetModuleInfo(handles[i], &info) == 0 && starts_with(info.name, "libkernel"))
         return handles[i];
   }
   return -1;
}

static void *kernel_function(int handle, const char *name)
{
   void *address = 0;
   if (sceKernelDlsym(handle, name, &address) != 0 || !address)
   {
      step("setup: not found:");
      step(name);
      return 0;
   }
   return address;
}

static void libc_setup(void)
{
   step("setup: finding libkernel");
   const int handle = libkernel_handle();
   if (handle < 0)
   {
      step("setup: libkernel not found");
      return;
   }
   if (!switched_off("/app0/libre-no-threads.txt"))
   {
      step("setup: thread callbacks");
      callback_setter dtors = (callback_setter)kernel_function(handle, "_sceKernelSetThreadDtors");
      callback_setter count = (callback_setter)kernel_function(handle, "_sceKernelSetThreadAtexitCount");
      callback_setter report = (callback_setter)kernel_function(handle, "_sceKernelSetThreadAtexitReport");
      if (dtors)
         (void)dtors((void *)thread_callback);
      if (count)
         (void)count((void *)thread_atexit_count);
      if (report)
         (void)report((void *)thread_callback);
      step("setup: thread callbacks done");
   }
   if (!switched_off("/app0/libre-no-heap.txt"))
   {
      step("setup: heap table");
      heap_api_setter set_heap = (heap_api_setter)kernel_function(handle, "_sceKernelRtldSetApplicationHeapAPI");
      heap_api[0] = (void *)__real_malloc;
      heap_api[1] = (void *)__real_free;
      heap_api[6] = (void *)__real_posix_memalign;
      if (set_heap)
         (void)set_heap(heap_api);
      step("setup: heap table done");
   }
}
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
   step("---- start");
   libc_setup();
   step("_init_env");
   _init_env(start_block);
   if (at_exit)
      (void)atexit(at_exit);
   (void)atexit(run_fini_array);

   step("preinit and init arrays");
   run_array(__preinit_array_start, __preinit_array_end);
   run_array(__init_array_start, __init_array_end);

   step("main");
   catchReturnFromMain(main(argc, argv, envp));
}
