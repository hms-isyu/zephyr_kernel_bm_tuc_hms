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

/*******************************************************************************
 * Variables
 ******************************************************************************/

static struct k_heap test_heap;

/* k_heap_init() rather than K_HEAP_DEFINE(), so this line is identical to the
 * one in the sys_heap adapter: same size, same alignment, same section. */
static uint8_t arena[Z_HEAP_MIN_SIZE_FOR(MEM_ARENA_SIZE)] __noinit
  __aligned(8);

/*******************************************************************************
 * Code
 ******************************************************************************/

/*
 * A heap carves the arena on demand, so it has no granularity to be cut at:
 * the argument is what a fixed-size allocator needs and this one ignores it.
 *
 * k_heap_init() lays out the wait queue as well as the arena, which is
 * harmless here because nothing ever pends on this heap, and is part of what
 * W6 separates from the sys_heap number.
 */
void mem_allocator_create_arena(size_t granularity)
{
  ARG_UNUSED(granularity);

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
 * Nothing to clog. The arena is the whole of what this allocator was given,
 * so it refuses at the arena's edge on its own and its capacity is its own.
 */
void mem_allocator_clog(void)
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
