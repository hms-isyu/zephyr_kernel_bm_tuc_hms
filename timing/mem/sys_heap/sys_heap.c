/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/*
 * Adapter: Zephyr sys_heap.
 *
 * The whole test. The workload is in mem_harness.c and never learns that this
 * allocator exists, let alone how it works.
 */

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/sys/sys_heap.h>

#include "mem_allocator.h"
#include "mem_harness.h"

/*******************************************************************************
 * Variables
 ******************************************************************************/

static struct sys_heap test_heap;

/* Z_HEAP_MIN_SIZE_FOR() turns the capacity the comparison is made at into the
 * array that provides it: the header chunk, the bucket table and the footer
 * are on top of MEM_ARENA_SIZE, not carved out of it, so every allocator here
 * can serve the top of the sweep at least once.
 *
 * Declared by hand rather than by K_HEAP_DEFINE() so the k_heap adapter can
 * carry the identical line and the delta between the two is the kernel wrapper
 * and nothing else. */
static uint8_t arena[Z_HEAP_MIN_SIZE_FOR(MEM_ARENA_SIZE)] __noinit
  __aligned(8);

/*******************************************************************************
 * Code
 ******************************************************************************/

/*
 * A heap carves the arena on demand, so it has no granularity to be cut at:
 * the argument is what a fixed-size allocator needs and this one ignores it.
 * The same arena, the same bytes, at every step of the sweep.
 *
 * W6 holds this call and nothing else.
 */
void mem_allocator_create_arena(size_t granularity)
{
  ARG_UNUSED(granularity);

  sys_heap_init(&test_heap, arena, sizeof(arena));
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
  return sys_heap_alloc(&test_heap, bytes);
}

void mem_allocator_free(void *allocation)
{
  sys_heap_free(&test_heap, allocation);
}

/*
 * No fit policy is declared here, and the interface has no place to declare
 * one. alloc_chunk() probes the size class the request lands in, then falls
 * back to one __builtin_ctz over the bitmap of non-empty classes and takes the
 * head of the smallest class above it. Whether that behaves as a first fit, a
 * best fit or neither is what W7_A, W7_B and W7_C answer between them, by
 * moving the free order and the roving pointer and recording which gap the
 * probe landed in.
 */

/* sys_heap_init() is the allocator laying out its own arena, so W6 is the
 * real number. */
bool mem_allocator_create_is_native(void)
{
  return true;
}
