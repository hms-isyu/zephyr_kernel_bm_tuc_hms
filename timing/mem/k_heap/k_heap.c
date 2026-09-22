/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/*
 * Adapter: Zephyr k_heap.
 *
 * The same algorithm as the sys_heap adapter with the kernel object around it.
 * The arena declaration below is character for character the sys_heap one, so
 * the delta between the two tests is the spinlock and the wait queue and
 * nothing else.
 *
 * K_NO_WAIT throughout: the workload defines full as a NULL return and an
 * allocation that blocks is not one the harness can time. The blocking path of
 * k_heap_alloc() is not covered here.
 */

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>

#include "mem_allocator.h"
#include "mem_harness.h"
#include "mem_zephyr_heap.h"

/*******************************************************************************
 * Variables
 ******************************************************************************/

static struct k_heap test_heap;

/* k_heap_init() rather than K_HEAP_DEFINE(), so this line is identical to the
 * one in the sys_heap adapter: same MEM_ARENA_SIZE, same alignment, same
 * section. The arena is the same storage at every step and for every
 * allocator in the comparison. */
static uint8_t arena[MEM_ARENA_SIZE] __noinit __aligned(8);

/*******************************************************************************
 * Code
 ******************************************************************************/

/*
 * A heap carves the arena on demand, so it has no granularity to be cut at and
 * the granularity is unused here. The whole array goes in at every step, the
 * same line the sys_heap adapter carries.
 *
 * What keeps the steps comparable is the count, not the size of the arena.
 * W3, W4 and W5 stop after N(s) allocations, so every step allocates the same
 * usable memory budget out of the same arena.
 *
 * k_heap_init() lays out the wait queue as well as the arena, which is
 * harmless here because nothing ever pends on this heap, and is part of what
 * W6 separates from the sys_heap number.
 *
 * W6 holds the one kernel call.
 */
void mem_allocator_create_arena(size_t granularity)
{
  (void) granularity;

  k_heap_init(&test_heap, arena, sizeof(arena));
}

/*
 * Nothing to tear down. The arena is an array this adapter owns, the creation
 * overwrites it in place, and the allocator holds nothing outside it that
 * could be given back.
 *
 * What stands here until the next creation is therefore the previous arena.
 * Nothing allocates out of it: the harness creates before it allocates, every
 * time.
 */
void mem_allocator_destroy_arena(void)
{
  return;
}

/*
 * Nothing to trim. The arena the creation handed over is the whole of what
 * this allocator was given, so its capacity is its own.
 */
void mem_allocator_trim_store(void)
{
  return;
}


void *mem_allocator_alloc(size_t bytes)
{
  return k_heap_alloc(&test_heap, bytes, K_NO_WAIT);
}

void mem_allocator_free(void *allocation)
{
  k_heap_free(&test_heap, allocation);
}

/*
 * The same algorithm as sys_heap, so the same three W7 runs are expected to
 * classify the same way; a difference between the two tests there would be the
 * wrapper changing the search, which is worth knowing.
 */
bool mem_allocator_create_is_native(void)
{
  return true;
}

/*
 * Never measured, and read only outside a window. A field read out of the
 * allocator's own state, not the number this adapter passed in. k_heap wraps
 * a sys_heap, so all three heap adapters read the same fields the same way.
 */
size_t mem_allocator_arena_bytes(void)
{
  return mem_zephyr_heap_real_arena(&test_heap.heap);
}

size_t mem_allocator_used_bytes(void)
{
  return mem_zephyr_heap_used_bytes(&test_heap.heap);
}

size_t mem_allocator_fixed_bytes(void)
{
  return mem_zephyr_heap_fixed_bytes(&test_heap.heap);
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
  return false;
}

