/*
 * RPCS3 for PS5 - virtual memory on the console (libre/vm.h).
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md), written from what RPCS3's
 * virtual memory needs of it (PS5_RPCS3's rpcs3/util/vm_native_ps5.cpp) and the
 * console's direct-memory calls as measured on the console.
 *
 * Committed memory is backed a unit (64 KiB) at a time: the units a commit finds
 * unbacked get one block of direct memory, mapped read-write at their place over
 * the reservation, zeroed, then given their protection. Which direct memory backs
 * each unit is kept in a table over the console's whole granted address space
 * (below 1 TiB): one pointer for every 64 MiB, then one entry for every unit.
 */

#include "libre/vm.h"

#include <pthread.h>
#include <stdlib.h>
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

/* The console's mapping flags: at the address given; and there only if nothing is */
#define MAP_AT 0x10
#define MAP_NOT_OVER 0x80
/* Write-back memory the CPU uses (the direct-memory type for ordinary memory) */
#define MEMORY_WRITE_BACK 0

/* The console's errors: EINVAL, ENOMEM */
#define ERROR_INVALID ((int)0x80020016)
#define ERROR_NO_MEMORY ((int)0x8002000c)

/* Shared memory mapped with no address goes from here up: above the GPU window,
 * the guest's ranges (0x10_0000_0000) and the title's heap, below RPCS3's own
 * search for ranges (0x40_0000_0000) */
#define VIEW_AREA 0x3000000000ull

#define ADDRESS_LIMIT 0x10000000000ull /* 1 TiB: nothing above is granted */
#define LEAF_UNITS 1024
#define LEAF_SPAN ((uint64_t)LEAF_UNITS * LIBRE_VM_UNIT)

/* For each unit, 1 + where its direct memory starts, or 0 when unbacked */
static int64_t *g_leaves[ADDRESS_LIMIT / LEAF_SPAN];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t round_down(uint64_t value, uint64_t to)
{
   return value & ~(to - 1);
}

static uint64_t round_up(uint64_t value, uint64_t to)
{
   return (value + to - 1) & ~(to - 1);
}

static int64_t *unit_entry(uint64_t unit, int create)
{
   if (unit >= ADDRESS_LIMIT)
      return NULL;
   int64_t **leaf = &g_leaves[unit / LEAF_SPAN];
   if (!*leaf)
   {
      if (!create)
         return NULL;
      *leaf = calloc(LEAF_UNITS, sizeof(int64_t));
      if (!*leaf)
         return NULL;
   }
   return &(*leaf)[(unit % LEAF_SPAN) / LIBRE_VM_UNIT];
}

/* Where the unit's direct memory starts, or -1 when it has none */
static int64_t unit_backing(uint64_t unit)
{
   const int64_t *entry = unit_entry(unit, 0);
   return entry && *entry ? *entry - 1 : -1;
}

/* A protection on mapped memory. Execute is granted only here, after mapping;
 * no access, which the kernel can refuse on direct memory, becomes read-only */
static int protect(uint64_t begin, uint64_t end, int protection)
{
   if (begin >= end)
      return 0;
   int result = sceKernelMprotect((void *)begin, end - begin, protection);
   if (result != 0 && protection == 0)
      result = sceKernelMprotect((void *)begin, end - begin, LIBRE_VM_READ);
   return result;
}

/* Makes mapped memory reserved space again, at once, in its place */
static int reserve_over(uint64_t begin, uint64_t size)
{
   void *at = (void *)begin;
   if (sceKernelReserveVirtualRange(&at, size, MAP_AT, 0) == 0 && at == (void *)begin)
      return 0;
   /* Not over a mapping: unmapped first, then reserved where it was */
   (void)sceKernelMunmap((void *)begin, size);
   at = (void *)begin;
   const int result = sceKernelReserveVirtualRange(&at, size, MAP_AT | MAP_NOT_OVER, 0);
   return result != 0 ? result : at == (void *)begin ? 0 : ERROR_NO_MEMORY;
}

int libre_vrange_reserve_at(void *address, size_t size)
{
   const uint64_t begin = (uint64_t)address;
   if (!size || begin % LIBRE_VM_UNIT || size % LIBRE_VM_UNIT || begin + size > ADDRESS_LIMIT)
      return ERROR_INVALID;
   void *at = address;
   const int result = sceKernelReserveVirtualRange(&at, size, MAP_AT | MAP_NOT_OVER, 0);
   if (result != 0)
      return result;
   if (at != address)
   {
      (void)sceKernelMunmap(at, size);
      return ERROR_NO_MEMORY;
   }
   return 0;
}

/* Backs the unbacked units in [first, last) with one block of direct memory */
static int back_units(uint64_t first, uint64_t last)
{
   const size_t size = last - first;
   int64_t start = 0;
   int result = sceKernelAllocateDirectMemory(0, (int64_t)sceKernelGetDirectMemorySize(), size, LIBRE_VM_UNIT,
      MEMORY_WRITE_BACK, &start);
   if (result != 0)
      return result;
   void *at = (void *)first;
   result = sceKernelMapDirectMemory(&at, size, LIBRE_VM_READ | LIBRE_VM_WRITE, MAP_AT, start, 0);
   if (result != 0 || at != (void *)first)
   {
      if (result == 0)
         (void)reserve_over((uint64_t)at, size);
      (void)sceKernelReleaseDirectMemory(start, size);
      return result != 0 ? result : ERROR_NO_MEMORY;
   }
   memset((void *)first, 0, size);
   for (uint64_t unit = first; unit < last; unit += LIBRE_VM_UNIT)
   {
      int64_t *entry = unit_entry(unit, 1);
      if (!entry)
         return ERROR_NO_MEMORY;
      *entry = start + (int64_t)(unit - first) + 1;
   }
   return 0;
}

int libre_vrange_commit(void *address, size_t size, int protection)
{
   if (!size)
      return 0;
   const uint64_t begin = round_down((uint64_t)address, LIBRE_VM_PAGE);
   const uint64_t end = round_up((uint64_t)address + size, LIBRE_VM_PAGE);
   if (end > ADDRESS_LIMIT || end < begin)
      return ERROR_INVALID;

   pthread_mutex_lock(&g_lock);
   int result = 0;
   const uint64_t last_unit = round_up(end, LIBRE_VM_UNIT);
   for (uint64_t unit = round_down(begin, LIBRE_VM_UNIT); unit < last_unit && result == 0;)
   {
      if (unit_backing(unit) >= 0)
      {
         unit += LIBRE_VM_UNIT;
         continue;
      }
      uint64_t run_end = unit + LIBRE_VM_UNIT;
      while (run_end < last_unit && unit_backing(run_end) < 0)
         run_end += LIBRE_VM_UNIT;
      result = back_units(unit, run_end);
      if (result == 0)
      {
         /* What the new units hold outside the pages asked for stays uncommitted */
         (void)protect(unit, begin > unit ? begin : unit, 0);
         (void)protect(end < run_end ? end : run_end, run_end, 0);
      }
      unit = run_end;
   }
   if (result == 0)
      result = protect(begin, end, protection);
   pthread_mutex_unlock(&g_lock);
   return result;
}

int libre_vrange_decommit(void *address, size_t size)
{
   if (!size)
      return 0;
   const uint64_t begin = round_down((uint64_t)address, LIBRE_VM_PAGE);
   const uint64_t end = round_up((uint64_t)address + size, LIBRE_VM_PAGE);
   if (end > ADDRESS_LIMIT || end < begin)
      return ERROR_INVALID;

   pthread_mutex_lock(&g_lock);
   int result = 0;
   for (uint64_t unit = round_down(begin, LIBRE_VM_UNIT); unit < end && result == 0; unit += LIBRE_VM_UNIT)
   {
      const int64_t backing = unit_backing(unit);
      if (backing < 0)
         continue;
      const uint64_t from = begin > unit ? begin : unit;
      const uint64_t to = end < unit + LIBRE_VM_UNIT ? end : unit + LIBRE_VM_UNIT;
      if (from == unit && to == unit + LIBRE_VM_UNIT)
      {
         /* The whole unit: reserved space again, its memory given back */
         result = reserve_over(unit, LIBRE_VM_UNIT);
         if (result == 0)
         {
            (void)sceKernelReleaseDirectMemory(backing, LIBRE_VM_UNIT);
            *unit_entry(unit, 0) = 0;
         }
      }
      else
      {
         /* Part of it: zeroed, for its next commit, and out of reach until then */
         result = sceKernelMprotect((void *)from, to - from, LIBRE_VM_READ | LIBRE_VM_WRITE);
         if (result == 0)
         {
            memset((void *)from, 0, to - from);
            (void)protect(from, to, 0);
         }
      }
   }
   pthread_mutex_unlock(&g_lock);
   return result;
}

int libre_vrange_release(void *address, size_t size)
{
   const uint64_t begin = (uint64_t)address;
   if (!size || begin % LIBRE_VM_UNIT || begin + size > ADDRESS_LIMIT)
      return ERROR_INVALID;

   pthread_mutex_lock(&g_lock);
   const int result = sceKernelMunmap(address, size);
   if (result == 0)
   {
      for (uint64_t unit = begin; unit < begin + size; unit += LIBRE_VM_UNIT)
      {
         const int64_t backing = unit_backing(unit);
         if (backing >= 0)
         {
            (void)sceKernelReleaseDirectMemory(backing, LIBRE_VM_UNIT);
            *unit_entry(unit, 0) = 0;
         }
      }
   }
   pthread_mutex_unlock(&g_lock);
   return result;
}

/* A place of its own for size bytes: the first free one from the view area up */
static int reserve_anywhere(size_t size, uint64_t *place)
{
   void *at = (void *)VIEW_AREA;
   const int result = sceKernelReserveVirtualRange(&at, size, 0, LIBRE_VM_UNIT);
   if (result != 0)
      return result;
   if ((uint64_t)at < VIEW_AREA)
   {
      (void)sceKernelMunmap(at, size);
      return ERROR_NO_MEMORY;
   }
   *place = (uint64_t)at;
   return 0;
}

/* Maps direct memory at a place: read-write, then the protection asked for */
static int map_at(uint64_t place, size_t size, int64_t start, int protection)
{
   void *at = (void *)place;
   int result = sceKernelMapDirectMemory(&at, size, LIBRE_VM_READ | LIBRE_VM_WRITE, MAP_AT, start, 0);
   if (result != 0)
      return result;
   if (at != (void *)place)
   {
      (void)sceKernelMunmap(at, size);
      return ERROR_NO_MEMORY;
   }
   if (protection != (LIBRE_VM_READ | LIBRE_VM_WRITE))
      result = protect(place, place + size, protection);
   return result;
}

int libre_shm_create(size_t size, struct libre_shm *shm)
{
   if (!size || !shm)
      return ERROR_INVALID;
   size = round_up(size, LIBRE_VM_UNIT);
   int64_t start = 0;
   int result = sceKernelAllocateDirectMemory(0, (int64_t)sceKernelGetDirectMemorySize(), size, LIBRE_VM_UNIT,
      MEMORY_WRITE_BACK, &start);
   if (result != 0)
      return result;

   /* Zeroed once, through a view of its own */
   uint64_t place = 0;
   result = reserve_anywhere(size, &place);
   if (result == 0)
   {
      result = map_at(place, size, start, LIBRE_VM_READ | LIBRE_VM_WRITE);
      if (result == 0)
         memset((void *)place, 0, size);
      (void)sceKernelMunmap((void *)place, size);
   }
   if (result != 0)
   {
      (void)sceKernelReleaseDirectMemory(start, size);
      return result;
   }
   shm->direct_start = start;
   shm->size = size;
   return 0;
}

int libre_shm_destroy(struct libre_shm *shm)
{
   if (!shm || !shm->size)
      return ERROR_INVALID;
   const int result = sceKernelReleaseDirectMemory(shm->direct_start, shm->size);
   if (result == 0)
      shm->size = 0;
   return result;
}

int libre_shm_map(const struct libre_shm *shm, size_t offset, size_t size, void *address, int protection,
   unsigned flags, void **view)
{
   if (!shm || !view || !size || offset % LIBRE_VM_UNIT || offset + size > shm->size)
      return ERROR_INVALID;
   size = round_up(size, LIBRE_VM_PAGE);

   uint64_t place = (uint64_t)address;
   const int fixed = (flags & LIBRE_SHM_FIXED) && address;
   if (fixed)
   {
      if (place % LIBRE_VM_UNIT)
         return ERROR_INVALID;
   }
   else
   {
      const int result = reserve_anywhere(size, &place);
      if (result != 0)
         return result;
   }
   const int result = map_at(place, size, shm->direct_start + (int64_t)offset, protection);
   if (result != 0)
   {
      if (!fixed)
         (void)sceKernelMunmap((void *)place, size);
      return result;
   }
   *view = (void *)place;
   return 0;
}

int libre_shm_unmap(void *view, size_t size, unsigned flags)
{
   if (!view || !size)
      return ERROR_INVALID;
   size = round_up(size, LIBRE_VM_PAGE);
   if (flags & LIBRE_SHM_KEEP_RESERVED)
      return reserve_over((uint64_t)view, size);
   return sceKernelMunmap(view, size);
}
