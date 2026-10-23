/*
 * RPCS3 for PS5 - the title's malloc family, on its heap (libre/heap.h).
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md). The link wraps these names
 * (--wrap=malloc and the rest), so every call the title makes comes here and goes
 * to the heap in direct memory. Blocks the console's own C library made (its
 * functions allocate with its own malloc, and the title frees what they return)
 * lie outside the heap's range and go back to it (__real_free and the rest).
 * What the heap cannot give, the console's C library gives instead, counted.
 *
 * getline and getdelim are here because a block they grow must be the heap's;
 * the console's C library has neither.
 */

#include "libre/heap.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

extern void *__real_malloc(size_t size);
extern void *__real_calloc(size_t count, size_t size);
extern void *__real_realloc(void *block, size_t size);
extern void __real_free(void *block);
extern int __real_posix_memalign(void **block, size_t alignment, size_t size);
extern size_t __real_malloc_usable_size(const void *block);

void libre_heap_count_fallback(void); /* memory/heap.c */

void *__wrap_malloc(size_t size);
void *__wrap_realloc(void *block, size_t size);
void __wrap_free(void *block);

static const struct libre_heap_tracker *_Atomic g_tracker;

void libre_heap_set_tracker(const struct libre_heap_tracker *tracker)
{
   atomic_store(&g_tracker, tracker);
}

static void *allocated(void *block, size_t size)
{
   const struct libre_heap_tracker *tracker = atomic_load_explicit(&g_tracker, memory_order_relaxed);
   if (block && tracker && tracker->allocated)
      tracker->allocated(block, size);
   return block;
}

static void freed(void *block)
{
   const struct libre_heap_tracker *tracker = atomic_load_explicit(&g_tracker, memory_order_relaxed);
   if (block && tracker && tracker->freed)
      tracker->freed(block);
}

static int power_of_two(size_t value)
{
   return value && !(value & (value - 1));
}

void *__wrap_malloc(size_t size)
{
   void *block = libre_heap_malloc(size);
   if (!block)
   {
      libre_heap_count_fallback();
      return __real_malloc(size);
   }
   return allocated(block, size);
}

void *__wrap_calloc(size_t count, size_t size)
{
   if (size && count > SIZE_MAX / size)
   {
      errno = ENOMEM;
      return NULL;
   }
   void *block = libre_heap_calloc(count, size);
   if (!block)
   {
      libre_heap_count_fallback();
      return __real_calloc(count, size);
   }
   return allocated(block, count * size);
}

void __wrap_free(void *block)
{
   if (!block)
      return;
   if (libre_heap_owns(block))
   {
      freed(block);
      libre_heap_free(block);
   }
   else
      __real_free(block);
}

void *__wrap_realloc(void *block, size_t size)
{
   if (!block)
      return __wrap_malloc(size);
   if (!libre_heap_owns(block))
   {
      /* The console's C library's block: into the heap, the old one given back */
      void *moved = __wrap_malloc(size);
      if (!moved)
         return NULL;
      const size_t old = __real_malloc_usable_size(block);
      memcpy(moved, block, old < size ? old : size);
      __real_free(block);
      return moved;
   }
   freed(block);
   void *moved = libre_heap_realloc(block, size);
   if (!moved)
   {
      /* Not resized, so still there: the console's C library makes the new one */
      libre_heap_count_fallback();
      void *fallback = __real_malloc(size);
      if (!fallback)
      {
         allocated(block, libre_heap_usable_size(block));
         return NULL;
      }
      const size_t old = libre_heap_usable_size(block);
      memcpy(fallback, block, old < size ? old : size);
      libre_heap_free(block);
      return fallback;
   }
   return allocated(moved, size);
}

void *__wrap_reallocarray(void *block, size_t count, size_t size)
{
   if (size && count > SIZE_MAX / size)
   {
      errno = ENOMEM;
      return NULL;
   }
   return __wrap_realloc(block, count * size);
}

void *__wrap_reallocf(void *block, size_t size)
{
   void *moved = __wrap_realloc(block, size);
   if (!moved && block && size)
      __wrap_free(block);
   return moved;
}

void *__wrap_memalign(size_t alignment, size_t size)
{
   void *block = libre_heap_memalign(alignment, size);
   if (!block)
   {
      libre_heap_count_fallback();
      void *fallback = NULL;
      return __real_posix_memalign(&fallback, alignment < sizeof(void *) ? sizeof(void *) : alignment, size) == 0
         ? fallback
         : NULL;
   }
   return allocated(block, size);
}

void *__wrap_aligned_alloc(size_t alignment, size_t size)
{
   if (!power_of_two(alignment))
   {
      errno = EINVAL;
      return NULL;
   }
   return __wrap_memalign(alignment, size);
}

int __wrap_posix_memalign(void **block, size_t alignment, size_t size)
{
   if (!block || !power_of_two(alignment) || alignment % sizeof(void *))
      return EINVAL;
   void *aligned = __wrap_memalign(alignment, size);
   if (!aligned)
      return ENOMEM;
   *block = aligned;
   return 0;
}

size_t __wrap_malloc_usable_size(const void *block)
{
   if (!block)
      return 0;
   return libre_heap_owns(block) ? libre_heap_usable_size(block) : __real_malloc_usable_size(block);
}

ssize_t __wrap_getdelim(char **line, size_t *capacity, int delimiter, FILE *stream)
{
   if (!line || !capacity || !stream)
   {
      errno = EINVAL;
      return -1;
   }
   if (!*line)
      *capacity = 0;
   size_t length = 0;
   for (;;)
   {
      const int c = fgetc(stream);
      if (c == EOF)
         break;
      /* Room for this character and the terminating zero */
      if (length + 2 > *capacity)
      {
         size_t grown = *capacity ? *capacity * 2 : 128;
         while (grown < length + 2)
            grown *= 2;
         char *moved = __wrap_realloc(*line, grown);
         if (!moved)
         {
            errno = ENOMEM;
            return -1;
         }
         *line = moved;
         *capacity = grown;
      }
      (*line)[length++] = (char)c;
      if (c == delimiter)
         break;
   }
   if (!length)
      return -1; /* end of file or an error, nothing read */
   (*line)[length] = '\0';
   return (ssize_t)length;
}

ssize_t __wrap_getline(char **line, size_t *capacity, FILE *stream)
{
   return __wrap_getdelim(line, capacity, '\n', stream);
}
