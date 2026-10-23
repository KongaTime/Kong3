/*
 * RPCS3 for PS5 - the title's heap, in direct memory.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md). The console's own C library
 * allocates from the title's flexible memory, about 400 MiB, which a title like
 * RPCS3 outgrows; this heap takes direct memory instead (12 GiB on the console),
 * in a range of its own, through dlmalloc's mspaces (third_party/dlmalloc, MIT-0).
 *
 * The title's malloc family goes here through the link's --wrap (memory/wrap.c):
 * malloc, calloc, realloc, free, posix_memalign, aligned_alloc, memalign,
 * malloc_usable_size, reallocf, reallocarray, getline and getdelim.
 */

#ifndef LIBRE_HEAP_H
#define LIBRE_HEAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *libre_heap_malloc(size_t size);
void *libre_heap_calloc(size_t count, size_t size);
void *libre_heap_realloc(void *block, size_t size);
void *libre_heap_memalign(size_t alignment, size_t size);
void libre_heap_free(void *block);
size_t libre_heap_usable_size(const void *block);

/* Whether a block is this heap's (else the console's C library made it) */
int libre_heap_owns(const void *block);

struct libre_heap_stats
{
   uint64_t range_base;   /* where the heap's range is */
   uint64_t range_bytes;  /* and its size */
   uint64_t mapped_bytes; /* the direct memory it holds now */
   uint64_t peak_bytes;   /* the most it has held */
   uint64_t fallbacks;    /* allocations it could not make, made by the console's C library instead */
};

void libre_heap_stats(struct libre_heap_stats *stats);

/* An observer of the title's allocations (a memory tracker): told of each block
 * the malloc family gives out and of each it takes back. Called on the
 * allocating thread, outside the heap's locks */
struct libre_heap_tracker
{
   void (*allocated)(void *block, size_t size);
   void (*freed)(void *block);
};

/* Sets the observer, or none with NULL */
void libre_heap_set_tracker(const struct libre_heap_tracker *tracker);

#ifdef __cplusplus
}
#endif

#endif
