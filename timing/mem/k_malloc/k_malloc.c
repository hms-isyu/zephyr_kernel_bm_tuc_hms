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
 * k_malloc also stores a heap back-pointer below every allocation, so the same
 * request costs one pointer more here than on the other two heaps. That is
 * what makes this the one allocator expected to refuse the top of the sweep:
 * 2^MEM_S_MAX bytes of payload plus the back-pointer is more than the capacity
 * the comparison is made at, so W1 records a NULL at s_max where the others
 * are served.
 */

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>

#include "mem_allocator.h"
#include "mem_harness.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*
 * kernel/mempool.c declares the system heap as
 * K_HEAP_DEFINE(_system_heap, Z_HEAP_MIN_SIZE_FOR(CONFIG_HEAP_MEM_POOL_SIZE)),
 * so this Kconfig number is already the capacity the comparison is made at and
 * the array around it is sized the same way the other two heap adapters size
 * theirs. Setting it to MEM_ARENA_SIZE puts all three on the same arena with
 * nothing to adjust.
 *
 * The Kconfig default is 0, which compiles k_malloc() out of the image
 * entirely.
 */
BUILD_ASSERT(CONFIG_HEAP_MEM_POOL_SIZE == MEM_ARENA_SIZE,
             "set CONFIG_HEAP_MEM_POOL_SIZE to MEM_ARENA_SIZE in prj.conf");

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
 * k_heap_init() over the system heap's own init_mem and init_bytes is that
 * call, and it is character for character the one kernel/kheap.c makes at
 * boot: K_HEAP_DEFINE() fills those two fields in with the array it declared,
 * and sys_heap_init() does not touch them afterwards. So the arena is neither
 * moved nor resized - it is the kernel's own array, sized from
 * CONFIG_HEAP_MEM_POOL_SIZE through Z_HEAP_MIN_SIZE_FOR() the same way the
 * other two heap adapters size theirs - and only the moment of the call is the
 * adapter's doing.
 *
 * Nothing else in this image allocates from the system heap, so laying it out
 * again takes nothing away from anyone.
 *
 * A heap carves the arena on demand, so the granularity is ignored.
 */
void mem_allocator_create_arena(size_t granularity)
{
  ARG_UNUSED(granularity);

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
 * Nothing to clog. CONFIG_HEAP_MEM_POOL_SIZE is the whole of what k_malloc has
 * - there is no store behind the system heap for it to take more from - so it
 * refuses at the arena's edge on its own.
 */
void mem_allocator_clog(void)
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
