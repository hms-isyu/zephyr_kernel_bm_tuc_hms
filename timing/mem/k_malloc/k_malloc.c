/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/*
 * Adapter: Zephyr k_malloc.
 *
 * Not a layer over k_heap on the allocating side: z_alloc_helper() in
 * kernel/mempool.c takes the spinlock itself and calls
 * sys_heap_noalign_alloc() directly, so k_malloc and k_heap_alloc are siblings
 * over one algorithm rather than a stack. k_free does go through
 * k_heap_free(). The alloc delta against k_heap is a difference of two
 * wrappers; on free it is a layer.
 *
 * k_malloc also stores a heap back-pointer below every allocation, and it
 * costs no chunk unit. z_alloc_helper() at kernel/mempool.c:33 adds
 * sizeof(heap_ref), 4 bytes, to the request, and k_malloc() reaches
 * sys_heap_alloc() through sys_heap_noalign_alloc(), which discards the
 * alignment argument. For a request of g bytes with g a multiple of 8 the
 * internal request is g + 4, and bytes_to_chunksz() in lib/heap/heap.h gives
 * (g + 4) / 8 + (4 + 4 + 7) / 8 = g / 8 + 1 chunk units, the same count a
 * plain g byte request costs on sys_heap: the back-pointer lands in the 4
 * bytes of slack the 4 byte chunk header leaves in its own 8 byte unit. The
 * harness only ever requests a power of two of at least 8, so the three heaps
 * are expected to report the same W1 served flags and the same W3 counts at
 * every step.
 */

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>

#include "mem_allocator.h"
#include "mem_harness.h"
#include "mem_zephyr_heap.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*
 * kernel/mempool.c:92 declares the system heap as
 * K_HEAP_DEFINE(_system_heap, Z_HEAP_MIN_SIZE_FOR(K_HEAP_MEM_POOL_SIZE)), so
 * this Kconfig number is the payload argument that array is sized through.
 * k_malloc cannot declare its own storage, the kernel owns it, so this symbol
 * is how k_malloc gets the same arena the other two heap adapters declare
 * directly. prj.conf cannot hold a macro, so this assert is what enforces the
 * agreement.
 *
 * At least, not equal: the kernel's array is sized through
 * Z_HEAP_MIN_SIZE_FOR(), which puts chunk 0 and the bucket table on top of the
 * payload argument, so the array is never smaller than the symbol. What the
 * comparison needs is that no step can run the arena out, and the arena
 * multiplier is what guarantees that.
 *
 * The Kconfig default is 0, which compiles k_malloc() out of the image
 * entirely.
 */
BUILD_ASSERT(CONFIG_HEAP_MEM_POOL_SIZE >= MEM_ARENA_SIZE,
             "set CONFIG_HEAP_MEM_POOL_SIZE in prj.conf to at least "
             "MEM_ARENA_SIZE, which is 4096 at a multiplier of 4");

/*******************************************************************************
 * Variables
 ******************************************************************************/

/* K_HEAP_DEFINE() in kernel/mempool.c, so this is a global and the adapter can
 * reach it. */
extern struct k_heap _system_heap;

/*******************************************************************************
 * Code
 ******************************************************************************/

/*
 * k_malloc does not create its arena: there is no k_malloc_init(), and the
 * system heap is laid out once by the kernel. But every workload here starts
 * from a known arena and W6 is the cost of laying one out, so the adapter has
 * to provide the call.
 *
 * k_heap_init() over the system heap's own init_mem is that call, the one
 * kernel/kheap.c makes at boot with the length taken from the table rather
 * than from the field beside the base. init_mem survives the re-creation:
 * sys_heap_init() in lib/heap/heap.c writes neither init_mem nor init_bytes,
 * only the static initializer behind K_HEAP_DEFINE() sets them, so init_mem
 * stays the base of the kernel's array across every creation this adapter
 * makes.
 *
 * The length is init_bytes, the whole of the kernel's array, at every step.
 * The arena is no longer a function of the step: it is the arena for all
 * three heap adapters, and what keeps the steps comparable is that W3, W4
 * and W5 stop after N(s) allocations rather than where the arena ran out.
 *
 * init_bytes has to be read before the creation overwrites nothing: the
 * static initializer behind K_HEAP_DEFINE() is the only writer of init_mem and
 * init_bytes, sys_heap_init() in lib/heap/heap.c writes neither, so both
 * survive every creation this adapter makes.
 *
 * Nothing else in this image allocates from the system heap, so laying it out
 * again takes nothing away from anyone.
 *
 * W6 holds the one kernel call.
 */
void mem_allocator_create_arena(size_t granularity)
{
  (void) granularity;

  k_heap_init(&_system_heap, _system_heap.heap.init_mem,
              _system_heap.heap.init_bytes);
}

/*
 * Nothing to tear down. The system heap's array is the kernel's, the creation
 * lays it out in place, and there is nothing to hand back to anyone.
 *
 * What stands here until the next creation is the previous arena. Nothing
 * allocates out of it: the harness creates before it allocates, every time.
 */
void mem_allocator_destroy_arena(void)
{
  return;
}

/*
 * Nothing to trim. The arena the creation handed over is the whole of what
 * k_malloc has, there is no store behind the system heap for it to take more
 * from, so its capacity is its own.
 */
void mem_allocator_trim_store(void)
{
  return;
}


void *mem_allocator_alloc(size_t bytes)
{
  return k_malloc(bytes);
}

void mem_allocator_free(void *allocation)
{
  k_free(allocation);
}

/*
 * One algorithm under all three heap adapters, so the three W7 runs are
 * expected to classify the same way here as on sys_heap.
 */

/* The kernel's own boot-time layout of this heap, so W6 is the real number
 * and compares against the other two heap adapters. */
bool mem_allocator_create_is_native(void)
{
  return true;
}

/*
 * Never measured, and read only outside a window. A field read out of the
 * kernel heap's own state, the same three readers the other two heap adapters
 * use, so the three space columns are produced identically for all three.
 */
size_t mem_allocator_arena_bytes(void)
{
  return mem_zephyr_heap_real_arena(&_system_heap.heap);
}

size_t mem_allocator_used_bytes(void)
{
  return mem_zephyr_heap_used_bytes(&_system_heap.heap);
}

size_t mem_allocator_fixed_bytes(void)
{
  return mem_zephyr_heap_fixed_bytes(&_system_heap.heap);
}

/*!
 * \brief Reports whether this allocator serves nothing larger than the
 *        granularity it was cut at.
 *
 * Always false: a heap carves the arena on demand and has no granularity to
 * be bounded by.
 */
bool mem_allocator_is_fixed_size(void)
{
  return false;
}

/*!
 * \brief Reports whether mem_allocator_alloc_n() is one call on this
 *        allocator rather than a stand-in that always refuses.
 *
 * Always false: this adapter has no batched allocation call of its own.
 */
bool mem_allocator_supports_alloc_n(void)
{
  return false;
}

/*!
 * \brief Allocates count pieces in one call. Without blocking.
 *
 * Not supported here: always refuses without allocating anything.
 *
 * \param [in]  count       Unused.
 * \param [out] allocations Unused.
 * \return false always.
 */
bool mem_allocator_alloc_n(size_t count, void **allocations)
{
  (void) count;
  (void) allocations;

  return false;
}

