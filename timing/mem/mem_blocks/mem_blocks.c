/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/*
 * Adapter: Zephyr sys_mem_blocks.
 *
 * Fixed-size pieces again, but over a bitmap rather than a list threaded
 * through the free pieces, and without a kernel object: there is no wait
 * queue, and no runtime init either, so where the slab adapter re-initializes
 * one object per step this one declares nine and switches between them.
 *
 * The nine share one buffer. Only one is ever live, the buffer contents are
 * never read, and each carries its own bitmap, which is the only state that
 * matters.
 *
 * sys_bitarray_alloc() scans 32-bit bundles from bit zero until one is not all
 * ones, so this is the one fixed-size allocator here whose allocation is not
 * constant time: the cost answers to the number of leading allocated pieces.
 * That is what W3 puts one window over.
 */

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/sys/mem_blocks.h>

#include "mem_allocator.h"
#include "mem_harness.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* The define takes ilog2 of the piece size. */
BUILD_ASSERT((MEM_ARENA_SIZE & (MEM_ARENA_SIZE - 1U)) == 0U,
             "the arena must be a power of two");

#define MB_BLOCKS_AT(s_) (MEM_ARENA_SIZE / (1U << (s_)))

/*
 * Which pool a granularity picks, as one count-trailing-zeros rather than a
 * search. The harness only ever asks for a power of two between 2^MEM_S_MIN
 * and 2^MEM_S_MAX, and W6 times this call, so the pool has to be reached in
 * constant time or the sweep would carry the length of the search.
 */
#define MB_STEP_OF(granularity_)                                                 ((uint32_t) __builtin_ctz((uint32_t) (granularity_)) - MEM_S_MIN)

/*******************************************************************************
 * Variables
 ******************************************************************************/

/* MEM_ARENA_SIZE exactly. The bitmap is beside the buffer rather than inside
 * it, so the capacity the comparison is made at and the array that provides it
 * are the same number here, as they are for the slab. */
static uint8_t arena[MEM_ARENA_SIZE] __noinit __aligned(8);

/* One pool per step of the sweep, 2^MEM_S_MIN to 2^MEM_S_MAX bytes a piece. */
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s3, 8, MB_BLOCKS_AT(3), arena);
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s4, 16, MB_BLOCKS_AT(4), arena);
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s5, 32, MB_BLOCKS_AT(5), arena);
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s6, 64, MB_BLOCKS_AT(6), arena);
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s7, 128, MB_BLOCKS_AT(7), arena);
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s8, 256, MB_BLOCKS_AT(8), arena);
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s9, 512, MB_BLOCKS_AT(9), arena);
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s10, 1024, MB_BLOCKS_AT(10),
                                          arena);

static sys_mem_blocks_t *const test_pool[MEM_S_COUNT] = {
  &pool_s3, &pool_s4, &pool_s5, &pool_s6,
  &pool_s7, &pool_s8, &pool_s9, &pool_s10,
};

/* The pool the arena is currently cut into, and the piece size it cuts at. */
static sys_mem_blocks_t *test_live        = &pool_s3;
static size_t            test_granularity = 0U;

/*******************************************************************************
 * Code
 ******************************************************************************/

/*
 * Picks the pool for this granularity and clears its bitmap, which is the
 * whole of a sys_mem_blocks pool's state: the buffer holds no links and no
 * headers, so a cleared bitmap is a pool that has never been allocated out of.
 *
 * sys_mem_blocks has no arena creation of its own - the pools are laid out at
 * compile time, which is why the adapter carries one per step of the sweep -
 * so this is the adapter writing the allocator's state rather than the
 * allocator laying out its own arena, and mem_allocator_create_is_native()
 * says so. The work is one word per 32 pieces, against the slab's one link per
 * piece, which is what the W6 sweep puts side by side.
 */
void mem_allocator_create_arena(size_t granularity)
{
  test_granularity = granularity;
  test_live        = test_pool[MB_STEP_OF(granularity)];

  for (uint32_t i = 0U; i < test_live->bitmap->num_bundles; i++)
  {
    test_live->bitmap->bundles[i] = 0U;
  }
}

/*
 * Nothing to tear down. The pools are laid out at compile time and share one
 * buffer, so there is nothing to give back and nothing the next creation does
 * not overwrite: it clears the bitmap of whichever pool it picks, and a
 * cleared bitmap is a pool that has never been allocated out of.
 *
 * What stands here until the next creation is the previous arena. Nothing
 * allocates out of it: the harness creates before it allocates, every time.
 */
void mem_allocator_destroy_arena(void)
{
  return;
}

/*
 * Nothing to clog. The buffer is the whole of what the pool was given, so it
 * refuses at the arena's edge on its own.
 */
void mem_allocator_clog(void)
{
  return;
}


/*
 * A request above the granularity is one this allocator cannot serve, and NULL
 * is the only way the interface has of saying so.
 */
void *mem_allocator_alloc(size_t bytes)
{
  void *allocation = NULL;

  if (bytes > test_granularity)
  {
    return NULL;
  }

  if (sys_mem_blocks_alloc(test_live, 1U, &allocation) != 0)
  {
    return NULL;
  }

  return allocation;
}

void mem_allocator_free(void *allocation)
{
  (void) sys_mem_blocks_free(test_live, 1U, &allocation);
}

/*
 * Layout L cannot be built out of one piece size, so all three W7 runs record
 * SETUP_FAILED, the same answer the slab gives and for the same reason.
 */

/* There is no sys_mem_blocks_init(); see mem_allocator_create_arena above. */
bool mem_allocator_create_is_native(void)
{
  return false;
}
