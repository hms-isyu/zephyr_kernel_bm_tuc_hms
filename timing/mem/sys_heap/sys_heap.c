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
#include "mem_zephyr_heap.h"

/*******************************************************************************
 * Variables
 ******************************************************************************/

static struct sys_heap test_heap;

/* The headroom arena: MEM_ARENA_BYTES, the same storage at every step and for
 * every allocator in the comparison. It is oversized on purpose, so that no
 * step can run it out and a refusal in W1 through W5 is a broken setup rather
 * than a property of the allocator.
 *
 * Declared by hand rather than by K_HEAP_DEFINE() so the k_heap adapter can
 * carry the identical line and the delta between the two is the kernel wrapper
 * and nothing else. */
static uint8_t arena[MEM_ARENA_BYTES] __noinit __aligned(8);

/*******************************************************************************
 * Code
 ******************************************************************************/

/*
 * A heap carves the arena on demand, so it has no granularity to be cut at and
 * the granularity is unused here. The whole array goes in at every step: the
 * arena is no longer a function of the step, which removes the last per
 * allocator and per step difference in the storage the adapters declare.
 *
 * What keeps the steps comparable is the count, not the size of the arena.
 * W3, W4 and W5 stop after N(s) allocations, so every step allocates the same
 * usable memory budget out of the same arena and the space it took is read
 * afterwards rather than inferred from where the arena ran out.
 *
 * W6 holds the one allocator call.
 */
void mem_allocator_create_arena(size_t granularity)
{
  (void) granularity;

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
 * Nothing to clog. The arena the creation handed over is the whole of what
 * this allocator was given, so its capacity is its own.
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

/*
 * Never measured, and read only outside a window. A field read out of the
 * allocator's own state, not the number this adapter passed in.
 */
size_t mem_allocator_arena_bytes(void)
{
  return mem_zephyr_heap_real_arena(&test_heap);
}

size_t mem_allocator_used_bytes(void)
{
  return mem_zephyr_heap_used_bytes(&test_heap);
}

size_t mem_allocator_fixed_bytes(void)
{
  return mem_zephyr_heap_fixed_bytes(&test_heap);
}

