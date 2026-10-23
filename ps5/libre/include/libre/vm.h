/*
 * RPCS3 for PS5 - virtual memory on the console: address ranges, the memory
 * committed in them, and shared memory.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md). Built on the console's
 * direct memory (libkernel's sceKernel*DirectMemory and sceKernelReserveVirtualRange),
 * whose behaviour was measured on the console: 16 KiB pages; a mapping asked for
 * with no address lands in the GPU window, so every mapping here has one; execute
 * asked for when mapping is refused, and granted afterwards by sceKernelMprotect;
 * from 0x10_0000_0000 up a range is reserved at the address asked for or refused;
 * released direct memory comes back with its old contents, so what is promised
 * fresh here is zeroed.
 *
 * Every function returns 0, or the console's error code (0x8002xxxx).
 */

#ifndef LIBRE_VM_H
#define LIBRE_VM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Protections: the console's own CPU bits, so they pass to sceKernelMprotect as they are */
#define LIBRE_VM_READ 0x1
#define LIBRE_VM_WRITE 0x2
#define LIBRE_VM_EXEC 0x4

/* The console's page, and the unit in which committed memory is backed */
#define LIBRE_VM_PAGE 0x4000
#define LIBRE_VM_UNIT 0x10000

/* Reserves [address, address + size) exactly there, or fails: never moved, never
 * over anything already there. address and size are multiples of LIBRE_VM_UNIT */
int libre_vrange_reserve_at(void *address, size_t size);

/* Backs the pages of a reserved range with memory and gives them a protection.
 * Pages not backed yet come zeroed; pages already backed keep their contents
 * (only their protection changes). The range is rounded out to whole pages */
int libre_vrange_commit(void *address, size_t size, int protection);

/* Gives the pages' memory back, leaving the range reserved: they read zero at
 * their next commit */
int libre_vrange_decommit(void *address, size_t size);

/* Gives back a whole range libre_vrange_reserve_at reserved, with its memory */
int libre_vrange_release(void *address, size_t size);

/* Shared memory: one block of direct memory, seen at one place or several */
struct libre_shm
{
   int64_t direct_start; /* where the block is in direct memory */
   size_t size;          /* its size, a multiple of LIBRE_VM_UNIT */
};

/* A new block, zeroed */
int libre_shm_create(size_t size, struct libre_shm *shm);

/* Gives the block back; its views must be unmapped first */
int libre_shm_destroy(struct libre_shm *shm);

/* libre_shm_map: at address exactly, over whatever reservation is there */
#define LIBRE_SHM_FIXED 0x1

/* Maps [offset, offset + size) of the block with a protection: at address with
 * LIBRE_SHM_FIXED, else in a place of its own. *view is where it is */
int libre_shm_map(const struct libre_shm *shm, size_t offset, size_t size, void *address, int protection,
   unsigned flags, void **view);

/* libre_shm_unmap: the view's place stays reserved */
#define LIBRE_SHM_KEEP_RESERVED 0x1

int libre_shm_unmap(void *view, size_t size, unsigned flags);

/* The console's own calls, for what the functions above do not cover: where a
 * mapping is and its protection, and changing the protection of mapped memory */
int sceKernelQueryMemoryProtection(void *address, void **start, void **end, uint32_t *protection);
int sceKernelMprotect(const void *address, size_t size, int protection);

#ifdef __cplusplus
}
#endif

#endif
