/*
 * RPCS3 for PS5 - the title's heap, in direct memory (libre/heap.h).
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md). dlmalloc (Doug Lea's
 * malloc 2.8.6, MIT-0, third_party/dlmalloc) manages the blocks, in mspaces; this
 * gives it its memory. The heap's range, 64 GiB from 0x20_0000_0000, is reserved
 * at the first allocation; what dlmalloc asks for is placed in it, backed with
 * direct memory a unit (64 KiB) at a time, and given back the same way. A table
 * at the start of the range, itself direct memory, records which direct memory
 * backs each unit, so dlmalloc may give back any part of what it took.
 *
 * Threads share a few arenas (mspaces), each with its own lock, so that they
 * seldom wait for one another; with FOOTERS every block knows its arena, so it
 * is freed or resized from any thread.
 */

#include "libre/heap.h"

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

/* The console's direct-memory calls (libkernel) */
extern int sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end, size_t size, size_t alignment,
   int memory_type, int64_t *start);
extern int sceKernelReleaseDirectMemory(int64_t start, size_t size);
extern size_t sceKernelGetDirectMemorySize(void);
extern int sceKernelMapDirectMemory(void **address, size_t size, int protection, int flags, int64_t start,
   size_t alignment);
extern int sceKernelReserveVirtualRange(void **address, size_t size, int flags, size_t alignment);
extern int sceKernelMunmap(void *address, size_t size);

#define MAP_AT 0x10
#define MAP_NOT_OVER 0x80
#define READ_WRITE 0x3
#define MEMORY_WRITE_BACK 0

#define RANGE_BASE 0x2000000000ull
#define RANGE_BYTES 0x1000000000ull /* 64 GiB */
#define UNIT 0x10000ull
#define UNITS (RANGE_BYTES / UNIT)
/* For each unit, 1 + the number of the direct-memory unit backing it, or 0 */
#define TABLE_BYTES (UNITS * sizeof(uint32_t))
#define FIRST_BLOCK (RANGE_BASE + TABLE_BYTES)

#define ARENAS 8

static void *heap_map(size_t size);
static int heap_unmap(void *address, size_t size);

/* dlmalloc: mspaces only, locked, footers, 32-byte blocks; memory from heap_map and heap_unmap
 * in 64 KiB units, its segments growing 4 MiB at a time, blocks of 4 MiB or more
 * each mapped on their own. Released direct memory keeps its old contents, so
 * calloc clears what it gives (MMAP_CLEARS 0). realloc(p, 0) keeps a block, as
 * the console's C library does (REALLOC_ZERO_BYTES_FREES not defined) */
#define ONLY_MSPACES 1
#define MSPACES 1
#define USE_LOCKS 1
#define USE_SPIN_LOCKS 0
#define FOOTERS 1
#define HAVE_MORECORE 0
#define HAVE_MMAP 1
#define HAVE_MREMAP 0
#define MMAP_CLEARS 0
#define MMAP(size) heap_map(size)
#define DIRECT_MMAP(size) heap_map(size)
#define MUNMAP(address, size) heap_unmap((address), (size))
#define malloc_getpagesize ((size_t)UNIT)
#define DEFAULT_GRANULARITY ((size_t)4 << 20)
#define DEFAULT_MMAP_THRESHOLD ((size_t)4 << 20)
#define NO_MALLOC_STATS 1
/* 32 bytes: the console's ABI says operator new gives that much
 * (__STDCPP_DEFAULT_NEW_ALIGNMENT__ 32), and the compiler stores 32 bytes at once
 * with aligned AVX moves into what it gives (build 159, with dlmalloc's 16: the
 * launcher's labels faulted in rsx::overlays::ps5ui::make_label, on my console) */
#define MALLOC_ALIGNMENT ((size_t)32)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include "../third_party/dlmalloc/malloc.c"
#pragma clang diagnostic pop

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static atomic_int g_ready;     /* 1 when the range and its table are there, -1 when they could not be */
static uint32_t *g_table;      /* at RANGE_BASE */
static uint64_t g_top;         /* the first unit never handed out */
static uint64_t g_mapped;
static uint64_t g_peak;
static atomic_uint_fast64_t g_fallbacks;

/* The parts of the range handed out once and given back: first fit, coalesced */
#define FREE_PARTS 4096
static struct
{
   uint64_t base, size;
} g_free[FREE_PARTS];
static int g_free_count;

static mspace g_arenas[ARENAS];

static void set_up(void)
{
   void *at = (void *)RANGE_BASE;
   if (sceKernelReserveVirtualRange(&at, RANGE_BYTES, MAP_AT | MAP_NOT_OVER, 0) != 0 || at != (void *)RANGE_BASE)
   {
      atomic_store(&g_ready, -1);
      return;
   }
   int64_t start = 0;
   at = (void *)RANGE_BASE;
   if (sceKernelAllocateDirectMemory(0, (int64_t)sceKernelGetDirectMemorySize(), TABLE_BYTES, UNIT, MEMORY_WRITE_BACK,
          &start) != 0 ||
      sceKernelMapDirectMemory(&at, TABLE_BYTES, READ_WRITE, MAP_AT, start, 0) != 0 || at != (void *)RANGE_BASE)
   {
      atomic_store(&g_ready, -1);
      return;
   }
   memset(at, 0, TABLE_BYTES);
   g_table = at;
   g_top = FIRST_BLOCK;
   g_mapped = g_peak = TABLE_BYTES;
   atomic_store(&g_ready, 1);
}

static int ready(void)
{
   if (atomic_load_explicit(&g_ready, memory_order_acquire) == 0)
      pthread_once(&g_once, set_up);
   return atomic_load_explicit(&g_ready, memory_order_acquire) == 1;
}

/* A place in the range for size bytes: a part given back, else new */
static uint64_t take_place(uint64_t size)
{
   for (int i = 0; i < g_free_count; i++)
   {
      if (g_free[i].size >= size)
      {
         const uint64_t place = g_free[i].base;
         g_free[i].base += size;
         g_free[i].size -= size;
         if (!g_free[i].size)
         {
            memmove(&g_free[i], &g_free[i + 1], (size_t)(g_free_count - i - 1) * sizeof g_free[0]);
            g_free_count--;
         }
         return place;
      }
   }
   if (g_top + size > RANGE_BASE + RANGE_BYTES)
      return 0;
   const uint64_t place = g_top;
   g_top += size;
   return place;
}

/* Gives a place back, joined to its neighbours, and to the top if it ends there */
static void give_place(uint64_t base, uint64_t size)
{
   int at = 0;
   while (at < g_free_count && g_free[at].base < base)
      at++;
   if (at > 0 && g_free[at - 1].base + g_free[at - 1].size == base)
   {
      at--;
      g_free[at].size += size;
   }
   else
   {
      if (g_free_count == FREE_PARTS)
         return; /* lost to reuse; still reserved, never handed out twice */
      memmove(&g_free[at + 1], &g_free[at], (size_t)(g_free_count - at) * sizeof g_free[0]);
      g_free[at].base = base;
      g_free[at].size = size;
      g_free_count++;
   }
   if (at + 1 < g_free_count && g_free[at].base + g_free[at].size == g_free[at + 1].base)
   {
      g_free[at].size += g_free[at + 1].size;
      memmove(&g_free[at + 1], &g_free[at + 2], (size_t)(g_free_count - at - 2) * sizeof g_free[0]);
      g_free_count--;
   }
   if (g_free[at].base + g_free[at].size == g_top)
   {
      g_top = g_free[at].base;
      memmove(&g_free[at], &g_free[at + 1], (size_t)(g_free_count - at - 1) * sizeof g_free[0]);
      g_free_count--;
   }
}

static uint32_t *entry(uint64_t unit)
{
   return &g_table[(unit - RANGE_BASE) / UNIT];
}

static void *heap_map(size_t size)
{
   if (!size || !ready())
      return MFAIL;
   size = (size + UNIT - 1) & ~(UNIT - 1);

   pthread_mutex_lock(&g_lock);
   const uint64_t place = take_place(size);
   int64_t start = 0;
   void *at = (void *)place;
   if (!place)
      goto fail;
   if (sceKernelAllocateDirectMemory(0, (int64_t)sceKernelGetDirectMemorySize(), size, UNIT, MEMORY_WRITE_BACK, &start) != 0)
   {
      give_place(place, size);
      goto fail;
   }
   if (sceKernelMapDirectMemory(&at, size, READ_WRITE, MAP_AT, start, 0) != 0 || at != (void *)place)
   {
      (void)sceKernelReleaseDirectMemory(start, size);
      give_place(place, size);
      goto fail;
   }
   for (uint64_t unit = 0; unit < size / UNIT; unit++)
      *entry(place + unit * UNIT) = (uint32_t)((uint64_t)start / UNIT + unit + 1);
   g_mapped += size;
   if (g_mapped > g_peak)
      g_peak = g_mapped;
   pthread_mutex_unlock(&g_lock);
   return at;

fail:
   pthread_mutex_unlock(&g_lock);
   return MFAIL;
}

static int heap_unmap(void *address, size_t size)
{
   const uint64_t base = (uint64_t)address;
   size = (size + UNIT - 1) & ~(UNIT - 1);
   if (base % UNIT || base < FIRST_BLOCK || base + size > RANGE_BASE + RANGE_BYTES)
      return -1;

   pthread_mutex_lock(&g_lock);
   /* Reserved space again at once, in its place, then its memory given back */
   void *at = address;
   if (sceKernelReserveVirtualRange(&at, size, MAP_AT, 0) != 0 || at != address)
   {
      /* Not over a mapping: unmapped first, then reserved where it was */
      (void)sceKernelMunmap(address, size);
      at = address;
      if (sceKernelReserveVirtualRange(&at, size, MAP_AT | MAP_NOT_OVER, 0) != 0 || at != address)
      {
         pthread_mutex_unlock(&g_lock);
         return -1;
      }
   }
   for (uint64_t unit = base; unit < base + size;)
   {
      const uint32_t first = *entry(unit);
      uint64_t run = 1;
      while (unit + run * UNIT < base + size && first && *entry(unit + run * UNIT) == first + run)
         run++;
      if (first)
         (void)sceKernelReleaseDirectMemory((int64_t)(first - 1) * (int64_t)UNIT, run * UNIT);
      for (uint64_t i = 0; i < run; i++)
         *entry(unit + i * UNIT) = 0;
      unit += run * UNIT;
   }
   g_mapped -= size;
   give_place(base, size);
   pthread_mutex_unlock(&g_lock);
   return 0;
}

/* This thread's arena, made at its first use: chosen by the thread's own handle,
 * not kept in thread-local storage, which the console's compiler emulates with
 * malloc (__emutls_get_address) and so cannot be used inside malloc */
static mspace arena(void)
{
   uint64_t self = (uint64_t)(uintptr_t)pthread_self();
   self ^= self >> 17;
   self *= 0x9e3779b97f4a7c15ull;
   const unsigned index = (unsigned)(self >> 61) % ARENAS;
   mspace space = __atomic_load_n(&g_arenas[index], __ATOMIC_ACQUIRE);
   if (space || !ready())
      return space;
   static pthread_mutex_t arenas_lock = PTHREAD_MUTEX_INITIALIZER;
   pthread_mutex_lock(&arenas_lock);
   space = g_arenas[index];
   if (!space)
   {
      space = create_mspace(0, 1);
      __atomic_store_n(&g_arenas[index], space, __ATOMIC_RELEASE);
   }
   pthread_mutex_unlock(&arenas_lock);
   return space;
}

int libre_heap_owns(const void *block)
{
   const uint64_t at = (uint64_t)block;
   return at >= FIRST_BLOCK && at < RANGE_BASE + RANGE_BYTES;
}

void *libre_heap_malloc(size_t size)
{
   const mspace space = arena();
   return space ? mspace_malloc(space, size) : NULL;
}

void *libre_heap_calloc(size_t count, size_t size)
{
   const mspace space = arena();
   return space ? mspace_calloc(space, count, size) : NULL;
}

void *libre_heap_realloc(void *block, size_t size)
{
   const mspace space = arena();
   return space ? mspace_realloc(space, block, size) : NULL;
}

void *libre_heap_memalign(size_t alignment, size_t size)
{
   const mspace space = arena();
   return space ? mspace_memalign(space, alignment, size) : NULL;
}

void libre_heap_free(void *block)
{
   /* The block's own arena, by its footer (FOOTERS: the argument is not used) */
   if (block)
      mspace_free(NULL, block);
}

size_t libre_heap_usable_size(const void *block)
{
   return block ? mspace_usable_size(block) : 0;
}

void libre_heap_count_fallback(void)
{
   atomic_fetch_add(&g_fallbacks, 1);
}

void libre_heap_stats(struct libre_heap_stats *stats)
{
   if (!stats)
      return;
   pthread_mutex_lock(&g_lock);
   stats->range_base = RANGE_BASE;
   stats->range_bytes = RANGE_BYTES;
   stats->mapped_bytes = g_mapped;
   stats->peak_bytes = g_peak;
   pthread_mutex_unlock(&g_lock);
   stats->fallbacks = atomic_load(&g_fallbacks);
}
